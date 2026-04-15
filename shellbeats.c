#define _GNU_SOURCE

// Suppress format-truncation warnings - we've made reasonable efforts to size buffers appropriately
#pragma GCC diagnostic ignored "-Wformat-truncation"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <ncurses.h>
#include <poll.h>
#include <pthread.h>  // NEW: for download thread
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <dirent.h>
#include <limits.h>
#include "youtube_playlist.h"
#include "surikata_sync.h"

#define MAX_RESULTS 150
#define DEFAULT_MAX_RESULTS 50
#define SHELLBEATS_VERSION "0.7"
#define MAX_PLAYLISTS 50
#define MAX_PLAYLIST_ITEMS 500
#define IPC_SOCKET "/tmp/shellbeats_mpv.sock"
#define CONFIG_DIR ".shellbeats"
#define PLAYLISTS_DIR "playlists"
#define PLAYLISTS_INDEX "playlists.json"
#define CONFIG_FILE "config.json"  // NEW: config file name
#define DOWNLOAD_QUEUE_FILE "download_queue.json"  // NEW: download queue file
#define MAX_DOWNLOAD_QUEUE 1000  // NEW: max download queue size
#define YTDLP_BIN_DIR "bin"
#define YTDLP_BINARY "yt-dlp"
#define YTDLP_VERSION_FILE "yt-dlp.version"

// ============================================================================
// Data Structures
// ============================================================================

// Song struct is defined in youtube_playlist.h

typedef struct {
    char *name;
    char *filename;
    Song items[MAX_PLAYLIST_ITEMS];
    int count;
    bool is_youtube_playlist;
    char* youtube_playlist_url;  // only set if is_youtube_playlist is true
    bool is_shared;             // Synced to Surikata as public
} Playlist;

// NEW: Configuration structure
typedef struct {
    char download_path[1024];
    int seek_step;           // Seek step in seconds (default 10)
    bool remember_session;   // Remember last session on exit
    int max_results;         // Search results limit (10-150, default 50)

    // YouTube cookies (since YouTube anti-bot block, see issue #44)
    int yt_cookies_mode;     // 0=off, 1=auto-from-browser, 2=manual file
    char yt_cookies_browser[16];  // firefox/chrome/chromium/brave/edge/safari/opera/vivaldi
    char yt_cookies_file[1024];   // path to cookies.txt for manual mode
} Config;

// Cookie modes
#define YT_COOKIES_OFF 0
#define YT_COOKIES_AUTO 1
#define YT_COOKIES_MANUAL 2

// NEW: Download task status
typedef enum {
    DOWNLOAD_PENDING,
    DOWNLOAD_ACTIVE,
    DOWNLOAD_COMPLETED,
    DOWNLOAD_FAILED
} DownloadStatus;

// NEW: Download task
typedef struct {
    char video_id[32];
    char title[512];
    char sanitized_filename[512];
    char playlist_name[256];  // empty string if not from playlist
    DownloadStatus status;
} DownloadTask;

// NEW: Download queue
typedef struct {
    DownloadTask tasks[MAX_DOWNLOAD_QUEUE];
    int count;
    int completed;
    int failed;
    int current_idx;  // currently downloading
    bool active;      // thread is running
    pthread_mutex_t mutex;
    pthread_t thread;
    bool thread_running;
    bool should_stop;
} DownloadQueue;

// NEW: Added VIEW_SETTINGS, VIEW_ABOUT
typedef enum {
    VIEW_SEARCH,
    VIEW_PLAYLISTS,
    VIEW_PLAYLIST_SONGS,
    VIEW_ADD_TO_PLAYLIST,
    VIEW_SETTINGS,
    VIEW_ABOUT,
    VIEW_SURISYNC
} ViewMode;

typedef enum {
    REPEAT_OFF = 0,
    REPEAT_ALL = 1,
    REPEAT_ONE = 2
} RepeatMode;

typedef struct {
    // Search results
    Song search_results[MAX_RESULTS];
    int search_count;
    int search_selected;
    int search_scroll;
    char query[256];
    
    // Playlists
    Playlist playlists[MAX_PLAYLISTS];
    int playlist_count;
    int playlist_selected;
    int playlist_scroll;
    
    // Current playlist view
    int current_playlist_idx;
    int playlist_song_selected;
    int playlist_song_scroll;
    
    // Playback state
    int playing_index;
    bool playing_from_playlist;
    int playing_playlist_idx;
    bool paused;
    
    // UI state
    ViewMode view;
    int add_to_playlist_selected;
    int add_to_playlist_scroll;
    Song *song_to_add;
    
    // NEW: Settings UI state
    int settings_selected;
    bool settings_editing;
    char settings_edit_buffer[1024];
    int settings_edit_pos;

    // YouTube bot-check detection (set by yt-dlp invocations)
    bool yt_blocked;
    
    // Playback timing (to ignore false end events during loading)
    time_t playback_started;
    
    // Config paths
    char config_dir[16384];      // Significantly increased buffer size
    char playlists_dir[16384];   // Significantly increased buffer size
    char playlists_index[16384]; // Significantly increased buffer size
    char config_file[16384];     // Significantly increased buffer size
    char download_queue_file[16384]; // Significantly increased buffer size

    // yt-dlp auto-update paths
    char ytdlp_bin_dir[1024];
    char ytdlp_local_path[1024];
    char ytdlp_version_file[1024];

    // NEW: Configuration
    Config config;

    // NEW: Download queue
    DownloadQueue download_queue;

    // yt-dlp auto-update state
    bool ytdlp_updating;
    bool ytdlp_update_done;
    bool ytdlp_has_local;
    pthread_t ytdlp_update_thread;
    bool ytdlp_update_thread_running;
    char ytdlp_update_status[128];

    // deno auto-install state (JS runtime for yt-dlp anti-bot)
    char deno_local_path[1024];
    bool deno_has_local;
    bool deno_installing;
    pthread_t deno_install_thread;
    bool deno_install_thread_running;
    char deno_install_status[128];

    // NEW: Spinner state for download progress
    int spinner_frame;
    time_t last_spinner_update;

    // SuriSync overlay state
    int surisync_selected;
    ViewMode surisync_return_view;  // View to return to on Esc
    bool surikata_online;           // Token verified at startup
    char latest_version[32];        // Latest version from server

    // Shuffle mode
    bool shuffle_mode;
    RepeatMode repeat_mode;

    // Seek step in seconds (configurable)
    int seek_step;

    // Remember session
    bool remember_session;
    char last_query[256];
    Song cached_search[MAX_RESULTS];
    int cached_search_count;
    int last_playlist_idx;
    int last_song_idx;
    bool was_playing_playlist;
} AppState;

// ============================================================================
// Globals
// ============================================================================

static pid_t mpv_pid = -1;
static int mpv_ipc_fd = -1;

// NEW: Global pointer for download thread access
static AppState *g_app_state = NULL;

// Logging system (activated with -log flag)
static FILE *g_log_file = NULL;

static void sb_log(const char *fmt, ...) {
    if (!g_log_file) return;
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    fprintf(g_log_file, "[%04d-%02d-%02d %02d:%02d:%02d] ",
            tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
            tm->tm_hour, tm->tm_min, tm->tm_sec);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log_file, fmt, ap);
    va_end(ap);
    fprintf(g_log_file, "\n");
    fflush(g_log_file);
}

// ============================================================================
// Forward Declarations
// ============================================================================

static void save_playlists_index(AppState *st);
static void save_playlist(AppState *st, int idx);
static void load_playlists(AppState *st);
static void load_playlist_songs(AppState *st, int idx);
static void save_config(AppState *st);  // NEW
static void load_config(AppState *st);  // NEW
static void augment_path_for_js_runtimes(void);  // re-augments PATH after deno install
static void auto_sync_playlist(AppState *st, int idx);
static int create_playlist(AppState *st, const char *name, bool is_youtube);
static void free_playlist_items(Playlist *pl);
static void free_all_playlists(AppState *st);
static void save_download_queue(AppState *st);  // NEW
static void load_download_queue(AppState *st);  // NEW
static void sanitize_name_for_path(const char *name, char *out, size_t out_size);
static void playlist_dir_name(const char *playlist_name, char *out, size_t out_size);

// ============================================================================
// Utility Functions
// ============================================================================

static char *trim_whitespace(char *s) {
    if (!s) return s;
    while (*s && isspace((unsigned char)*s)) s++;
    if (*s == 0) return s;
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return s;
}

static bool file_exists(const char *path) {
    struct stat sb;
    return stat(path, &sb) == 0;
}

static bool dir_exists(const char *path) {
    struct stat sb;
    return stat(path, &sb) == 0 && S_ISDIR(sb.st_mode);
}

// NEW: Create directory recursively (like mkdir -p)
static bool mkdir_p(const char *path) {
    char tmp[4096]; // Increased buffer size
    char *p = NULL;
    size_t len;
    
    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);
    
    if (tmp[len - 1] == '/') {
        tmp[len - 1] = 0;
    }
    
    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            if (!dir_exists(tmp)) {
                if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
                    return false;
                }
            }
            *p = '/';
        }
    }
    
    if (!dir_exists(tmp)) {
        if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
            return false;
        }
    }
    
    return true;
}

// NEW: Sanitize title for filename
static void sanitize_title_for_filename(const char *title, const char *video_id, 
                                         char *out, size_t out_size) {
    if (!title || !video_id || !out || out_size < 32) {
        if (out && out_size > 0) out[0] = '\0';
        return;
    }
    
    char sanitized[256] = {0};
    size_t j = 0;
    
    for (size_t i = 0; title[i] && j < sizeof(sanitized) - 1; i++) {
        char c = title[i];
        
        // Replace problematic characters
        if (c == '/' || c == '\\' || c == ':' || c == '*' || 
            c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
            continue;
        } else if (c == ' ' || c == '\'' || c == '`') {
            if (j > 0 && sanitized[j-1] != '_') {
                sanitized[j++] = '_';
            }
        } else if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' ||
                   (unsigned char)c > 127) {  // Allow UTF-8
            sanitized[j++] = c;
        }
    }
    
    // Remove trailing underscores
    while (j > 0 && sanitized[j-1] == '_') {
        j--;
    }
    sanitized[j] = '\0';
    
    // If empty after sanitization, use "download"
    if (sanitized[0] == '\0') {
        strcpy(sanitized, "download");
    }
    
    // Truncate if too long (leave room for _[video_id].mp3)
    if (strlen(sanitized) > 180) {
        sanitized[180] = '\0';
    }
    
    // Build final filename: Title_[video_id].mp3
    snprintf(out, out_size, "%s_[%s].mp3", sanitized, video_id);
}

// NEW: Check if a file for this video_id exists in directory
static bool file_exists_for_video(const char *dir_path, const char *video_id) {
    DIR *dir = opendir(dir_path);
    if (!dir) return false;
    
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "[%s].mp3", video_id);
    
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strstr(entry->d_name, pattern) != NULL) {
            closedir(dir);
            return true;
        }
    }
    
    closedir(dir);
    return false;
}

// Get the full path to a local file for a song in a playlist
// Returns true if file exists and fills out_path, false otherwise
static bool get_local_file_path_for_song(AppState *st, const char *playlist_name,
                                         const char *video_id, char *out_path, size_t out_size) {
    if (!video_id || !video_id[0] || !out_path || out_size == 0) return false;

    char dest_dir[4096]; // Increased buffer size
    if (playlist_name && playlist_name[0]) {
        char safe_name[512];
        playlist_dir_name(playlist_name, safe_name, sizeof(safe_name));
        snprintf(dest_dir, sizeof(dest_dir), "%s/%s", st->config.download_path, safe_name);
    } else {
        snprintf(dest_dir, sizeof(dest_dir), "%s", st->config.download_path);
    }

    // Search for file with this video_id
    DIR *dir = opendir(dest_dir);
    if (!dir) return false;

    char pattern[64];
    snprintf(pattern, sizeof(pattern), "[%s].mp3", video_id);

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strstr(entry->d_name, pattern) != NULL) {
            snprintf(out_path, out_size, "%s/%s", dest_dir, entry->d_name);
            closedir(dir);
            return true;
        }
    }

    closedir(dir);
    return false;
}

// Recursively delete a directory and all its contents
static bool delete_directory_recursive(const char *path) {
    DIR *dir = opendir(path);
    if (!dir) return false;

    struct dirent *entry;
    char filepath[4096]; // Increased buffer size
    bool success = true;

    while ((entry = readdir(dir)) != NULL) {
        // Skip . and ..
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        snprintf(filepath, sizeof(filepath), "%s/%s", path, entry->d_name);

        struct stat st;
        if (stat(filepath, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                // Recursively delete subdirectory
                if (!delete_directory_recursive(filepath)) {
                    success = false;
                }
            } else {
                // Delete file
                if (unlink(filepath) != 0) {
                    success = false;
                }
            }
        }
    }

    closedir(dir);

    // Delete the directory itself
    if (rmdir(path) != 0) {
        success = false;
    }

    return success;
}

static char *json_escape_string(const char *s) {
    if (!s) return strdup("");
    
    size_t len = strlen(s);
    size_t alloc = len * 2 + 1;
    char *out = malloc(alloc);
    if (!out) return NULL;
    
    size_t j = 0;
    for (size_t i = 0; i < len && j < alloc - 2; i++) {
        char c = s[i];
        if (c == '"' || c == '\\') {
            out[j++] = '\\';
        } else if (c == '\n') {
            out[j++] = '\\';
            out[j++] = 'n';
            continue;
        } else if (c == '\r') {
            out[j++] = '\\';
            out[j++] = 'r';
            continue;
        } else if (c == '\t') {
            out[j++] = '\\';
            out[j++] = 't';
            continue;
        }
        out[j++] = c;
    }
    out[j] = '\0';
    return out;
}

// Simple JSON string extraction (finds "key":"value" and returns value)
static char *json_get_string(const char *json, const char *key) {
    char pattern[256];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    
    const char *p = strstr(json, pattern);
    if (!p) return NULL;
    
    p += strlen(pattern);
    while (*p && (*p == ' ' || *p == ':' || *p == '\t')) p++;
    
    if (*p != '"') return NULL;
    p++;
    
    const char *start = p;
    while (*p && *p != '"') {
        if (*p == '\\' && *(p+1)) p++;
        p++;
    }
    
    size_t len = p - start;
    char *result = malloc(len + 1);
    if (!result) return NULL;
    
    // Unescape
    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        if (start[i] == '\\' && i + 1 < len) {
            i++;
            if (start[i] == 'n') result[j++] = '\n';
            else if (start[i] == 'r') result[j++] = '\r';
            else if (start[i] == 't') result[j++] = '\t';
            else result[j++] = start[i];
        } else {
            result[j++] = start[i];
        }
    }
    result[j] = '\0';
    return result;
}

// ============================================================================
// Config Directory Management
// ============================================================================

static bool init_config_dirs(AppState *st) {
    const char *home = getenv("HOME");
    if (!home) {
        home = "/tmp";
    }

    // XDG_CONFIG_HOME support: prefer $XDG_CONFIG_HOME/shellbeats if it exists
    const char *xdg = getenv("XDG_CONFIG_HOME");
    if (xdg && xdg[0] != '\0') {
        char xdg_path[PATH_MAX];
        snprintf(xdg_path, sizeof(xdg_path), "%s/shellbeats", xdg);
        if (dir_exists(xdg_path)) {
            snprintf(st->config_dir, sizeof(st->config_dir), "%s", xdg_path);
        } else {
            snprintf(st->config_dir, sizeof(st->config_dir), "%s/%s", home, CONFIG_DIR);
        }
    } else {
        snprintf(st->config_dir, sizeof(st->config_dir), "%s/%s", home, CONFIG_DIR);
    }
    snprintf(st->playlists_dir, sizeof(st->playlists_dir), "%s/%s", st->config_dir, PLAYLISTS_DIR);
    snprintf(st->playlists_index, sizeof(st->playlists_index), "%s/%s", st->config_dir, PLAYLISTS_INDEX);
    snprintf(st->config_file, sizeof(st->config_file), "%s/%s", st->config_dir, CONFIG_FILE);  // NEW
    snprintf(st->download_queue_file, sizeof(st->download_queue_file), "%s/%s", st->config_dir, DOWNLOAD_QUEUE_FILE);  // NEW

    // yt-dlp auto-update paths
    snprintf(st->ytdlp_bin_dir, sizeof(st->ytdlp_bin_dir), "%s/%s", st->config_dir, YTDLP_BIN_DIR);
    snprintf(st->ytdlp_local_path, sizeof(st->ytdlp_local_path), "%s/%s", st->ytdlp_bin_dir, YTDLP_BINARY);
    snprintf(st->ytdlp_version_file, sizeof(st->ytdlp_version_file), "%s/%s", st->config_dir, YTDLP_VERSION_FILE);

    // deno auto-install path (shares ytdlp_bin_dir for simplicity)
    snprintf(st->deno_local_path, sizeof(st->deno_local_path), "%s/deno", st->ytdlp_bin_dir);
    st->deno_has_local = false;
    st->deno_installing = false;
    st->deno_install_thread_running = false;
    st->deno_install_status[0] = '\0';

    st->config_dir[sizeof(st->config_dir) - 1] = '\0';
    st->playlists_dir[sizeof(st->playlists_dir) - 1] = '\0';
    st->playlists_index[sizeof(st->playlists_index) - 1] = '\0';
    st->config_file[sizeof(st->config_file) - 1] = '\0';  // NEW
    st->download_queue_file[sizeof(st->download_queue_file) - 1] = '\0';  // NEW

    // Create config directory if not exists
    if (!dir_exists(st->config_dir)) {
        if (mkdir(st->config_dir, 0755) != 0) {
            return false;
        }
    }

    // Create playlists directory if not exists
    if (!dir_exists(st->playlists_dir)) {
        if (mkdir(st->playlists_dir, 0755) != 0) {
            return false;
        }
    }

    // Create bin directory for local yt-dlp (non-fatal: auto-update is optional)
    if (!dir_exists(st->ytdlp_bin_dir)) {
        mkdir(st->ytdlp_bin_dir, 0755);  // best-effort, app works without it
    }
    
    // Create empty playlists index if not exists
    if (!file_exists(st->playlists_index)) {
        FILE *f = fopen(st->playlists_index, "w");
        if (f) {
            fprintf(f, "{\"playlists\":[]}\n");
            fclose(f);
        }
    }
    
    return true;
}

// ============================================================================
// yt-dlp Auto-Update System
// ============================================================================

// Returns the path to the yt-dlp binary to use.
// Prefers local copy in ~/.shellbeats/bin/yt-dlp, falls back to system yt-dlp.
static const char *get_ytdlp_cmd(AppState *st) {
    if (st->ytdlp_has_local && file_exists(st->ytdlp_local_path)) {
        return st->ytdlp_local_path;
    }
    return "yt-dlp";
}

// Build cookie args string for yt-dlp invocations.
// out is a buffer that will be filled with " --cookies-from-browser X" or
// " --cookies /path" (with leading space) or empty string if no cookies.
// out_size should be at least 1100 bytes for safe path handling.
static void build_cookie_args(AppState *st, char *out, size_t out_size) {
    if (!out || out_size == 0) return;
    out[0] = '\0';

    if (st->config.yt_cookies_mode == YT_COOKIES_AUTO) {
        if (st->config.yt_cookies_browser[0]) {
            snprintf(out, out_size, " --cookies-from-browser %s",
                     st->config.yt_cookies_browser);
        }
    } else if (st->config.yt_cookies_mode == YT_COOKIES_MANUAL) {
        if (st->config.yt_cookies_file[0]) {
            // Quote the path to handle spaces. Single quotes are safe since
            // we don't expect the path to contain them.
            snprintf(out, out_size, " --cookies '%s'", st->config.yt_cookies_file);
        }
    }
}

// Copy text to system clipboard. Tries pbcopy (macOS), wl-copy (Wayland),
// xclip (X11), xsel (X11 fallback). Returns true on success.
static bool copy_to_clipboard(const char *text) {
    if (!text) return false;

    const char *copiers[] = {
        "pbcopy",                              // macOS
        "wl-copy",                             // Wayland
        "xclip -selection clipboard",          // X11
        "xsel --clipboard --input",            // X11 fallback
        NULL
    };

    for (int i = 0; copiers[i]; i++) {
        // Check the binary exists before running
        char check[64];
        char first_word[32];
        const char *space = strchr(copiers[i], ' ');
        size_t wlen = space ? (size_t)(space - copiers[i]) : strlen(copiers[i]);
        if (wlen >= sizeof(first_word)) continue;
        memcpy(first_word, copiers[i], wlen);
        first_word[wlen] = '\0';
        snprintf(check, sizeof(check), "command -v %s >/dev/null 2>&1", first_word);
        if (system(check) != 0) continue;

        FILE *p = popen(copiers[i], "w");
        if (!p) continue;
        fputs(text, p);
        int rc = pclose(p);
        if (rc == 0) return true;
    }
    return false;
}

static void *ytdlp_update_thread_func(void *arg) {
    AppState *st = (AppState *)arg;

    sb_log("yt-dlp update thread started");
    sb_log("  local_path: %s", st->ytdlp_local_path);
    sb_log("  version_file: %s", st->ytdlp_version_file);
    sb_log("  bin_dir: %s", st->ytdlp_bin_dir);
    sb_log("  bin_dir exists: %s", dir_exists(st->ytdlp_bin_dir) ? "yes" : "no");

    snprintf(st->ytdlp_update_status, sizeof(st->ytdlp_update_status),
             "Checking for yt-dlp updates...");

    // Detect available download tool (curl or wget)
    bool has_curl = (system("command -v curl >/dev/null 2>&1") == 0);
    bool has_wget = (system("command -v wget >/dev/null 2>&1") == 0);
    sb_log("  has_curl: %s, has_wget: %s", has_curl ? "yes" : "no", has_wget ? "yes" : "no");

    if (!has_curl && !has_wget) {
        sb_log("  ABORT: no curl or wget found");
        snprintf(st->ytdlp_update_status, sizeof(st->ytdlp_update_status),
                 "No curl or wget found");
        st->ytdlp_updating = false;
        st->ytdlp_update_done = true;
        return NULL;
    }

    // Get latest version tag by following the GitHub redirect
    char version_cmd[512];
    if (has_curl) {
        snprintf(version_cmd, sizeof(version_cmd),
                 "curl -sL -o /dev/null -w '%%{url_effective}' "
                 "'https://github.com/yt-dlp/yt-dlp/releases/latest' 2>/dev/null");
    } else {
        snprintf(version_cmd, sizeof(version_cmd),
                 "wget --spider -S --max-redirect=5 "
                 "'https://github.com/yt-dlp/yt-dlp/releases/latest' 2>&1 "
                 "| grep -i 'Location:' | tail -1 | awk '{print $2}'");
    }
    sb_log("  version_cmd: %s", version_cmd);

    FILE *fp = popen(version_cmd, "r");
    if (!fp) {
        sb_log("  ABORT: popen failed for version check");
        snprintf(st->ytdlp_update_status, sizeof(st->ytdlp_update_status),
                 "Update check failed");
        st->ytdlp_updating = false;
        st->ytdlp_update_done = true;
        return NULL;
    }

    char redirect_url[512] = {0};
    if (!fgets(redirect_url, sizeof(redirect_url), fp)) {
        redirect_url[0] = '\0';
    }
    int pclose_ret = pclose(fp);
    sb_log("  redirect_url: '%s' (pclose=%d)", redirect_url, pclose_ret);

    // Extract version tag from URL (e.g., .../tag/2025.01.26)
    char *tag = strrchr(redirect_url, '/');
    if (!tag || strlen(tag) < 2) {
        sb_log("  ABORT: could not extract tag from redirect_url");
        snprintf(st->ytdlp_update_status, sizeof(st->ytdlp_update_status),
                 "No network or failed to check version");
        st->ytdlp_updating = false;
        st->ytdlp_update_done = true;
        return NULL;
    }
    tag++;

    // Trim whitespace/newlines
    size_t tag_len = strlen(tag);
    while (tag_len > 0 && (tag[tag_len - 1] == '\n' || tag[tag_len - 1] == '\r' ||
           tag[tag_len - 1] == ' ')) {
        tag[--tag_len] = '\0';
    }
    sb_log("  parsed tag: '%s'", tag);

    if (tag_len == 0) {
        sb_log("  ABORT: empty tag after trimming");
        snprintf(st->ytdlp_update_status, sizeof(st->ytdlp_update_status),
                 "Could not parse yt-dlp version");
        st->ytdlp_updating = false;
        st->ytdlp_update_done = true;
        return NULL;
    }

    // Check local version — skip download if already up to date
    bool needs_download = true;
    sb_log("  checking local version file: %s (exists=%s)",
           st->ytdlp_version_file, file_exists(st->ytdlp_version_file) ? "yes" : "no");
    sb_log("  checking local binary: %s (exists=%s)",
           st->ytdlp_local_path, file_exists(st->ytdlp_local_path) ? "yes" : "no");

    if (file_exists(st->ytdlp_version_file) && file_exists(st->ytdlp_local_path)) {
        FILE *vf = fopen(st->ytdlp_version_file, "r");
        if (vf) {
            char local_ver[128] = {0};
            if (fgets(local_ver, sizeof(local_ver), vf)) {
                size_t lv_len = strlen(local_ver);
                while (lv_len > 0 && (local_ver[lv_len - 1] == '\n' ||
                       local_ver[lv_len - 1] == '\r')) {
                    local_ver[--lv_len] = '\0';
                }
                sb_log("  local_ver: '%s' vs remote: '%s'", local_ver, tag);
                if (strcmp(local_ver, tag) == 0) {
                    needs_download = false;
                }
            }
            fclose(vf);
        }
    }

    if (!needs_download) {
        sb_log("  already up to date, skipping download");
        snprintf(st->ytdlp_update_status, sizeof(st->ytdlp_update_status),
                 "yt-dlp is up to date (%s)", tag);
        st->ytdlp_has_local = true;
        st->ytdlp_updating = false;
        st->ytdlp_update_done = true;
        return NULL;
    }

    // Download latest yt-dlp binary
    sb_log("  needs download, starting...");
    snprintf(st->ytdlp_update_status, sizeof(st->ytdlp_update_status),
             "Downloading yt-dlp %s...", tag);

    char dl_cmd[2048];
    if (has_curl) {
        snprintf(dl_cmd, sizeof(dl_cmd),
                 "curl -sL 'https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp' "
                 "-o '%s' 2>/dev/null && chmod +x '%s'",
                 st->ytdlp_local_path, st->ytdlp_local_path);
    } else {
        snprintf(dl_cmd, sizeof(dl_cmd),
                 "wget -q 'https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp' "
                 "-O '%s' 2>/dev/null && chmod +x '%s'",
                 st->ytdlp_local_path, st->ytdlp_local_path);
    }
    sb_log("  dl_cmd: %s", dl_cmd);

    int result = system(dl_cmd);
    sb_log("  download result: %d", result);
    sb_log("  file exists after download: %s", file_exists(st->ytdlp_local_path) ? "yes" : "no");

    if (result == 0 && file_exists(st->ytdlp_local_path)) {
        FILE *vf = fopen(st->ytdlp_version_file, "w");
        if (vf) {
            fprintf(vf, "%s\n", tag);
            fclose(vf);
            sb_log("  version file written: %s", tag);
        } else {
            sb_log("  WARN: could not write version file");
        }
        st->ytdlp_has_local = true;
        snprintf(st->ytdlp_update_status, sizeof(st->ytdlp_update_status),
                 "yt-dlp updated to %s", tag);
        sb_log("  SUCCESS: yt-dlp updated to %s", tag);
    } else {
        snprintf(st->ytdlp_update_status, sizeof(st->ytdlp_update_status),
                 "yt-dlp download failed");
        sb_log("  FAILED: download failed (result=%d)", result);
    }

    st->ytdlp_updating = false;
    st->ytdlp_update_done = true;
    sb_log("yt-dlp update thread finished");
    return NULL;
}

static void start_ytdlp_update(AppState *st) {
    if (st->ytdlp_update_thread_running) return;

    st->ytdlp_has_local = file_exists(st->ytdlp_local_path);
    st->ytdlp_updating = true;
    st->ytdlp_update_done = false;

    if (pthread_create(&st->ytdlp_update_thread, NULL,
                       ytdlp_update_thread_func, st) == 0) {
        st->ytdlp_update_thread_running = true;
    } else {
        st->ytdlp_updating = false;
    }
}

static void stop_ytdlp_update(AppState *st) {
    if (!st->ytdlp_update_thread_running) return;
    pthread_join(st->ytdlp_update_thread, NULL);
    st->ytdlp_update_thread_running = false;
}

// ============================================================================
// deno auto-install (JS runtime needed by yt-dlp for YouTube anti-bot)
// Downloads deno binary into ~/.shellbeats/bin/deno on first run if no JS
// runtime is detected (deno/node/bun in PATH, or local binary already there).
// Runs in background thread so UI is not blocked.
// ============================================================================

static bool js_runtime_already_available(AppState *st) {
    // Check our own ~/.shellbeats/bin/deno
    if (file_exists(st->deno_local_path)) return true;
    // Check system PATH for deno/node/bun
    if (system("command -v deno >/dev/null 2>&1") == 0) return true;
    if (system("command -v node >/dev/null 2>&1") == 0) return true;
    if (system("command -v bun >/dev/null 2>&1") == 0) return true;
    return false;
}

static void *deno_install_thread_func(void *arg) {
    AppState *st = (AppState *)arg;

    sb_log("[DENO] thread started");

    // Opt-out via env var (useful for CI / Docker / scripted setups)
    if (getenv("SHELLBEATS_NO_AUTO_DENO")) {
        sb_log("[DENO] SHELLBEATS_NO_AUTO_DENO is set, skipping install");
        st->deno_install_thread_running = false;
        return NULL;
    }

    // Skip if any JS runtime is already available (unless forced for debug)
    bool force_install = (getenv("SHELLBEATS_FORCE_DENO_INSTALL") != NULL);
    if (!force_install && js_runtime_already_available(st)) {
        sb_log("[DENO] JS runtime already available, skipping install");
        st->deno_has_local = file_exists(st->deno_local_path);
        st->deno_install_thread_running = false;
        return NULL;
    }
    if (force_install) {
        sb_log("[DENO] SHELLBEATS_FORCE_DENO_INSTALL set, forcing install");
    }

    // Detect OS and architecture
    char arch[64] = "x86_64";
    bool is_macos = false;

    FILE *fp = popen("uname -m 2>/dev/null", "r");
    if (fp) {
        if (fgets(arch, sizeof(arch), fp)) {
            size_t len = strlen(arch);
            while (len > 0 && (arch[len-1] == '\n' || arch[len-1] == '\r')) {
                arch[--len] = '\0';
            }
        }
        pclose(fp);
    }

    fp = popen("uname -s 2>/dev/null", "r");
    if (fp) {
        char osbuf[32] = "";
        if (fgets(osbuf, sizeof(osbuf), fp)) {
            if (strstr(osbuf, "Darwin")) is_macos = true;
        }
        pclose(fp);
    }

    sb_log("[DENO] detected: arch=%s, os=%s", arch, is_macos ? "darwin" : "linux");

    // Build URL for the right binary
    bool is_arm = (strstr(arch, "aarch64") || strstr(arch, "arm64"));
    const char *deno_arch = is_arm ? "aarch64" : "x86_64";
    char url[512];
    if (is_macos) {
        snprintf(url, sizeof(url),
                 "https://github.com/denoland/deno/releases/latest/download/deno-%s-apple-darwin.zip",
                 deno_arch);
    } else {
        snprintf(url, sizeof(url),
                 "https://github.com/denoland/deno/releases/latest/download/deno-%s-unknown-linux-gnu.zip",
                 deno_arch);
    }

    sb_log("[DENO] download URL: %s", url);
    snprintf(st->deno_install_status, sizeof(st->deno_install_status),
             "Downloading deno (~30MB, one-time)...");
    st->deno_installing = true;

    // Ensure bin dir exists
    if (!dir_exists(st->ytdlp_bin_dir)) {
        if (mkdir(st->ytdlp_bin_dir, 0755) != 0 && errno != EEXIST) {
            sb_log("[DENO] failed to create bin dir %s: %s",
                   st->ytdlp_bin_dir, strerror(errno));
            snprintf(st->deno_install_status, sizeof(st->deno_install_status),
                     "deno install failed: cannot create bin dir");
            st->deno_installing = false;
            st->deno_install_thread_running = false;
            return NULL;
        }
    }

    // Download to /tmp
    char zip_path[1024];
    snprintf(zip_path, sizeof(zip_path), "/tmp/shellbeats_deno_%d.zip", (int)getpid());

    char cmd[2048];
    bool has_curl = (system("command -v curl >/dev/null 2>&1") == 0);
    bool has_wget = (system("command -v wget >/dev/null 2>&1") == 0);

    if (has_curl) {
        snprintf(cmd, sizeof(cmd),
                 "curl -sL --fail -o '%s' '%s' >/dev/null 2>&1",
                 zip_path, url);
    } else if (has_wget) {
        snprintf(cmd, sizeof(cmd),
                 "wget -q -O '%s' '%s' >/dev/null 2>&1",
                 zip_path, url);
    } else {
        sb_log("[DENO] no curl/wget available, cannot download");
        snprintf(st->deno_install_status, sizeof(st->deno_install_status),
                 "deno install failed: no curl/wget");
        st->deno_installing = false;
        st->deno_install_thread_running = false;
        return NULL;
    }

    sb_log("[DENO] downloading: %s", cmd);
    int rc = system(cmd);
    sb_log("[DENO] download exit=%d", rc);

    // Verify the zip was downloaded and is reasonably sized (>5MB)
    struct stat zst;
    if (rc != 0 || stat(zip_path, &zst) != 0 || zst.st_size < 5000000) {
        long long sz = (stat(zip_path, &zst) == 0) ? (long long)zst.st_size : 0;
        sb_log("[DENO] download failed or file too small (size=%lld)", sz);
        unlink(zip_path);
        snprintf(st->deno_install_status, sizeof(st->deno_install_status),
                 "deno download failed (network?)");
        st->deno_installing = false;
        st->deno_install_thread_running = false;
        return NULL;
    }
    sb_log("[DENO] download ok, zip size=%lld bytes", (long long)zst.st_size);

    snprintf(st->deno_install_status, sizeof(st->deno_install_status),
             "Extracting deno...");

    // Extract: try unzip, then python3 zipfile
    bool has_unzip = (system("command -v unzip >/dev/null 2>&1") == 0);
    if (has_unzip) {
        snprintf(cmd, sizeof(cmd),
                 "unzip -o -q '%s' -d '%s' >/dev/null 2>&1",
                 zip_path, st->ytdlp_bin_dir);
        sb_log("[DENO] extracting with unzip: %s", cmd);
        rc = system(cmd);
        sb_log("[DENO] unzip exit=%d", rc);
    } else {
        rc = -1;
    }

    if (rc != 0 || !file_exists(st->deno_local_path)) {
        // Fallback: python3 zipfile module
        bool has_python3 = (system("command -v python3 >/dev/null 2>&1") == 0);
        if (has_python3) {
            snprintf(cmd, sizeof(cmd),
                     "python3 -c \"import zipfile; zipfile.ZipFile('%s').extractall('%s')\" >/dev/null 2>&1",
                     zip_path, st->ytdlp_bin_dir);
            sb_log("[DENO] fallback python3 extraction: %s", cmd);
            rc = system(cmd);
            sb_log("[DENO] python3 exit=%d", rc);
        } else {
            sb_log("[DENO] no python3 either, extraction failed");
        }
    }

    unlink(zip_path);

    if (file_exists(st->deno_local_path)) {
        chmod(st->deno_local_path, 0755);
        st->deno_has_local = true;
        sb_log("[DENO] installed successfully at %s", st->deno_local_path);
        snprintf(st->deno_install_status, sizeof(st->deno_install_status),
                 "deno installed (YouTube ready)");
        // Re-augment PATH so subsequent fork/execs (mpv, yt-dlp) see it
        augment_path_for_js_runtimes();
    } else {
        sb_log("[DENO] extraction failed - deno not at %s", st->deno_local_path);
        snprintf(st->deno_install_status, sizeof(st->deno_install_status),
                 "deno extract failed (install unzip or python3)");
    }

    st->deno_installing = false;
    st->deno_install_thread_running = false;
    return NULL;
}

static void start_deno_install(AppState *st) {
    if (st->deno_install_thread_running) return;

    // Quick pre-check: if a runtime is already there, skip thread entirely
    // (unless forced via env var for debug)
    st->deno_has_local = file_exists(st->deno_local_path);
    bool force_install = (getenv("SHELLBEATS_FORCE_DENO_INSTALL") != NULL);
    if (!force_install && js_runtime_already_available(st)) {
        sb_log("[DENO] runtime already available, no install needed");
        return;
    }

    if (pthread_create(&st->deno_install_thread, NULL,
                       deno_install_thread_func, st) == 0) {
        st->deno_install_thread_running = true;
    } else {
        sb_log("[DENO] pthread_create failed: %s", strerror(errno));
    }
}

static void stop_deno_install(AppState *st) {
    if (!st->deno_install_thread_running) return;
    pthread_join(st->deno_install_thread, NULL);
    st->deno_install_thread_running = false;
}

static const char *repeat_mode_label(RepeatMode mode) {
    switch (mode) {
        case REPEAT_ALL: return "ALL";
        case REPEAT_ONE: return "ONE";
        case REPEAT_OFF:
        default: return "OFF";
    }
}

static RepeatMode next_repeat_mode(RepeatMode mode) {
    switch (mode) {
        case REPEAT_OFF: return REPEAT_ALL;
        case REPEAT_ALL: return REPEAT_ONE;
        case REPEAT_ONE:
        default: return REPEAT_OFF;
    }
}

// ============================================================================
// NEW: Configuration Persistence
// ============================================================================

static void init_default_config(AppState *st) {
    const char *home = getenv("HOME");
    if (!home) home = "/tmp";

    // Default download path: ~/Music/shellbeats
    snprintf(st->config.download_path, sizeof(st->config.download_path),
             "%s/Music/shellbeats", home);

    // Default seek step: 10 seconds
    st->config.seek_step = 10;

    // Default: don't remember session
    st->config.remember_session = false;

    // Default: 50 search results
    st->config.max_results = DEFAULT_MAX_RESULTS;

    // Default: cookies disabled (use yt-dlp without auth flags)
    st->config.yt_cookies_mode = YT_COOKIES_OFF;
    st->config.yt_cookies_browser[0] = '\0';
    st->config.yt_cookies_file[0] = '\0';
}

static void save_config(AppState *st) {
    FILE *f = fopen(st->config_file, "w");
    if (!f) return;

    char *escaped_path = json_escape_string(st->config.download_path);
    char *escaped_query = json_escape_string(st->last_query);

    fprintf(f, "{\n");
    fprintf(f, "  \"download_path\": \"%s\",\n", escaped_path ? escaped_path : "");
    fprintf(f, "  \"seek_step\": %d,\n", st->config.seek_step);
    fprintf(f, "  \"max_results\": %d,\n", st->config.max_results);
    fprintf(f, "  \"remember_session\": %s,\n", st->config.remember_session ? "true" : "false");
    fprintf(f, "  \"shuffle_mode\": %s,\n", st->shuffle_mode ? "true" : "false");
    fprintf(f, "  \"repeat_mode\": %d,\n", (int)st->repeat_mode);

    // YouTube cookies config
    char *escaped_browser = json_escape_string(st->config.yt_cookies_browser);
    char *escaped_cookies_file = json_escape_string(st->config.yt_cookies_file);
    fprintf(f, "  \"yt_cookies_mode\": %d,\n", st->config.yt_cookies_mode);
    fprintf(f, "  \"yt_cookies_browser\": \"%s\",\n", escaped_browser ? escaped_browser : "");
    fprintf(f, "  \"yt_cookies_file\": \"%s\",\n", escaped_cookies_file ? escaped_cookies_file : "");
    free(escaped_browser);
    free(escaped_cookies_file);

    // Session state (only saved if remember_session is enabled)
    if (st->config.remember_session) {
        fprintf(f, "  \"last_query\": \"%s\",\n", escaped_query ? escaped_query : "");
        fprintf(f, "  \"last_playlist_idx\": %d,\n", st->last_playlist_idx);
        fprintf(f, "  \"last_song_idx\": %d,\n", st->last_song_idx);
        fprintf(f, "  \"was_playing_playlist\": %s,\n", st->was_playing_playlist ? "true" : "false");

        // Save cached search results
        fprintf(f, "  \"cached_search_count\": %d,\n", st->cached_search_count);
        fprintf(f, "  \"cached_search\": [\n");
        for (int i = 0; i < st->cached_search_count; i++) {
            char *esc_title = json_escape_string(st->cached_search[i].title);
            char *esc_vid = json_escape_string(st->cached_search[i].video_id);
            char *esc_url = json_escape_string(st->cached_search[i].url);
            fprintf(f, "    {\"title\": \"%s\", \"video_id\": \"%s\", \"url\": \"%s\", \"duration\": %d}%s\n",
                    esc_title ? esc_title : "",
                    esc_vid ? esc_vid : "",
                    esc_url ? esc_url : "",
                    st->cached_search[i].duration,
                    (i < st->cached_search_count - 1) ? "," : "");
            free(esc_title);
            free(esc_vid);
            free(esc_url);
        }
        fprintf(f, "  ]\n");
    } else {
        fprintf(f, "  \"last_query\": \"\",\n");
        fprintf(f, "  \"cached_search_count\": 0,\n");
        fprintf(f, "  \"cached_search\": []\n");
    }

    fprintf(f, "}\n");

    free(escaped_path);
    free(escaped_query);
    fclose(f);
}

static int json_get_int(const char *json, const char *key, int default_val) {
    char pattern[256];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);

    const char *p = strstr(json, pattern);
    if (!p) return default_val;

    p += strlen(pattern);
    while (*p && (*p == ' ' || *p == ':' || *p == '\t')) p++;

    if (!*p) return default_val;

    // Check for true/false
    if (strncmp(p, "true", 4) == 0) return 1;
    if (strncmp(p, "false", 5) == 0) return 0;

    return atoi(p);
}

static bool json_get_bool(const char *json, const char *key, bool default_val) {
    return json_get_int(json, key, default_val ? 1 : 0) != 0;
}

static void load_config(AppState *st) {
    // Set defaults first
    init_default_config(st);
    st->config.seek_step = 10;
    st->config.remember_session = false;
    st->shuffle_mode = false;
    st->repeat_mode = REPEAT_OFF;
    st->last_query[0] = '\0';
    st->cached_search_count = 0;
    st->last_playlist_idx = -1;
    st->last_song_idx = -1;
    st->was_playing_playlist = false;

    FILE *f = fopen(st->config_file, "r");
    if (!f) {
        // No config file, save defaults
        save_config(st);
        return;
    }

    // Read entire file
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fsize <= 0 || fsize > 256 * 1024) {
        fclose(f);
        return;
    }

    char *content = malloc(fsize + 1);
    if (!content) {
        fclose(f);
        return;
    }

    size_t read_size = fread(content, 1, fsize, f);
    content[read_size] = '\0';
    fclose(f);

    // Parse download_path
    char *download_path = json_get_string(content, "download_path");
    if (download_path && download_path[0]) {
        strncpy(st->config.download_path, download_path, sizeof(st->config.download_path) - 1);
        st->config.download_path[sizeof(st->config.download_path) - 1] = '\0';
    }
    free(download_path);

    // Parse new config options
    st->config.seek_step = json_get_int(content, "seek_step", 10);
    if (st->config.seek_step < 1) st->config.seek_step = 10;
    if (st->config.seek_step > 300) st->config.seek_step = 300;

    st->config.max_results = json_get_int(content, "max_results", DEFAULT_MAX_RESULTS);
    if (st->config.max_results < 10) st->config.max_results = 10;
    if (st->config.max_results > 150) st->config.max_results = 150;

    st->config.remember_session = json_get_bool(content, "remember_session", false);
    st->shuffle_mode = json_get_bool(content, "shuffle_mode", false);
    int repeat_val = json_get_int(content, "repeat_mode", REPEAT_OFF);
    if (repeat_val < REPEAT_OFF || repeat_val > REPEAT_ONE) repeat_val = REPEAT_OFF;
    st->repeat_mode = (RepeatMode)repeat_val;

    // YouTube cookies config
    int cookies_mode = json_get_int(content, "yt_cookies_mode", YT_COOKIES_OFF);
    if (cookies_mode < YT_COOKIES_OFF || cookies_mode > YT_COOKIES_MANUAL) cookies_mode = YT_COOKIES_OFF;
    st->config.yt_cookies_mode = cookies_mode;
    char *cookies_browser = json_get_string(content, "yt_cookies_browser");
    if (cookies_browser) {
        snprintf(st->config.yt_cookies_browser, sizeof(st->config.yt_cookies_browser), "%s", cookies_browser);
        free(cookies_browser);
    } else {
        st->config.yt_cookies_browser[0] = '\0';
    }
    char *cookies_file = json_get_string(content, "yt_cookies_file");
    if (cookies_file) {
        snprintf(st->config.yt_cookies_file, sizeof(st->config.yt_cookies_file), "%s", cookies_file);
        free(cookies_file);
    } else {
        st->config.yt_cookies_file[0] = '\0';
    }

    // Parse session state
    char *last_query = json_get_string(content, "last_query");
    if (last_query) {
        strncpy(st->last_query, last_query, sizeof(st->last_query) - 1);
        st->last_query[sizeof(st->last_query) - 1] = '\0';
        free(last_query);
    }

    st->last_playlist_idx = json_get_int(content, "last_playlist_idx", -1);
    st->last_song_idx = json_get_int(content, "last_song_idx", -1);
    st->was_playing_playlist = json_get_bool(content, "was_playing_playlist", false);
    st->cached_search_count = json_get_int(content, "cached_search_count", 0);
    if (st->cached_search_count > MAX_RESULTS) st->cached_search_count = MAX_RESULTS;

    // Parse cached search results
    if (st->cached_search_count > 0) {
        const char *arr = strstr(content, "\"cached_search\"");
        if (arr) {
            arr = strchr(arr, '[');
            if (arr) {
                arr++;
                for (int i = 0; i < st->cached_search_count; i++) {
                    const char *obj_start = strchr(arr, '{');
                    if (!obj_start) break;
                    const char *obj_end = strchr(obj_start, '}');
                    if (!obj_end) break;

                    size_t obj_len = obj_end - obj_start + 1;
                    char *obj = malloc(obj_len + 1);
                    if (!obj) break;
                    memcpy(obj, obj_start, obj_len);
                    obj[obj_len] = '\0';

                    char *title = json_get_string(obj, "title");
                    char *vid = json_get_string(obj, "video_id");
                    char *url = json_get_string(obj, "url");
                    int dur = json_get_int(obj, "duration", 0);

                    st->cached_search[i].title = title;
                    st->cached_search[i].video_id = vid;
                    st->cached_search[i].url = url;
                    st->cached_search[i].duration = dur;

                    free(obj);
                    arr = obj_end + 1;
                }
            }
        }
    }

    free(content);
}

// ============================================================================
// NEW: Download Queue Persistence
// ============================================================================

// NOTE: Must be called with download_queue.mutex already locked
static void save_download_queue(AppState *st) {
    FILE *f = fopen(st->download_queue_file, "w");
    if (!f) {
        return;
    }

    fprintf(f, "{\n  \"tasks\": [\n");

    bool first = true;
    for (int i = 0; i < st->download_queue.count; i++) {
        DownloadTask *task = &st->download_queue.tasks[i];

        // Only save pending or failed tasks
        if (task->status != DOWNLOAD_PENDING && task->status != DOWNLOAD_FAILED) {
            continue;
        }

        char *escaped_title = json_escape_string(task->title);
        char *escaped_filename = json_escape_string(task->sanitized_filename);
        char *escaped_playlist = json_escape_string(task->playlist_name);

        const char *status_str = task->status == DOWNLOAD_FAILED ? "failed" : "pending";

        if (!first) fprintf(f, ",\n");
        first = false;

        fprintf(f, "    {\"video_id\": \"%s\", \"title\": \"%s\", \"filename\": \"%s\", \"playlist\": \"%s\", \"status\": \"%s\"}",
                task->video_id,
                escaped_title ? escaped_title : "",
                escaped_filename ? escaped_filename : "",
                escaped_playlist ? escaped_playlist : "",
                status_str);

        free(escaped_title);
        free(escaped_filename);
        free(escaped_playlist);
    }

    fprintf(f, "\n  ]\n}\n");
    fclose(f);
}

static void load_download_queue(AppState *st) {
    FILE *f = fopen(st->download_queue_file, "r");
    if (!f) return;
    
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    if (fsize <= 0 || fsize > 1024 * 1024) {
        fclose(f);
        return;
    }
    
    char *content = malloc(fsize + 1);
    if (!content) {
        fclose(f);
        return;
    }
    
    size_t read_size = fread(content, 1, fsize, f);
    content[read_size] = '\0';
    fclose(f);
    
    const char *p = strstr(content, "\"tasks\"");
    if (!p) {
        free(content);
        return;
    }
    
    p = strchr(p, '[');
    if (!p) {
        free(content);
        return;
    }
    
    pthread_mutex_lock(&st->download_queue.mutex);
    
    while (st->download_queue.count < MAX_DOWNLOAD_QUEUE) {
        const char *obj_start = strchr(p, '{');
        if (!obj_start) break;
        
        const char *obj_end = strchr(obj_start, '}');
        if (!obj_end) break;
        
        size_t obj_len = obj_end - obj_start + 1;
        char *obj = malloc(obj_len + 1);
        if (!obj) break;
        
        memcpy(obj, obj_start, obj_len);
        obj[obj_len] = '\0';
        
        char *video_id = json_get_string(obj, "video_id");
        char *title = json_get_string(obj, "title");
        char *filename = json_get_string(obj, "filename");
        char *playlist = json_get_string(obj, "playlist");
        char *status_str = json_get_string(obj, "status");
        
        if (video_id && video_id[0]) {
            DownloadTask *task = &st->download_queue.tasks[st->download_queue.count];
            
            strncpy(task->video_id, video_id, sizeof(task->video_id) - 1);
            strncpy(task->title, title ? title : "", sizeof(task->title) - 1);
            strncpy(task->sanitized_filename, filename ? filename : "", sizeof(task->sanitized_filename) - 1);
            strncpy(task->playlist_name, playlist ? playlist : "", sizeof(task->playlist_name) - 1);
            
            if (status_str && strcmp(status_str, "failed") == 0) {
                task->status = DOWNLOAD_FAILED;
                st->download_queue.failed++;
            } else {
                task->status = DOWNLOAD_PENDING;
            }
            
            st->download_queue.count++;
        }
        
        free(video_id);
        free(title);
        free(filename);
        free(playlist);
        free(status_str);
        free(obj);
        
        p = obj_end + 1;
    }
    
    pthread_mutex_unlock(&st->download_queue.mutex);
    free(content);
}

// ============================================================================
// NEW: Download Thread
// ============================================================================

static void *download_thread_func(void *arg) {
    AppState *st = (AppState *)arg;
    
    while (!st->download_queue.should_stop) {
        pthread_mutex_lock(&st->download_queue.mutex);
        
        // Find next pending task
        int task_idx = -1;
        for (int i = 0; i < st->download_queue.count; i++) {
            if (st->download_queue.tasks[i].status == DOWNLOAD_PENDING) {
                task_idx = i;
                st->download_queue.tasks[i].status = DOWNLOAD_ACTIVE;
                st->download_queue.current_idx = i;
                st->download_queue.active = true;
                break;
            }
        }
        
        if (task_idx < 0) {
            // No pending tasks
            st->download_queue.active = false;
            st->download_queue.current_idx = -1;
            pthread_mutex_unlock(&st->download_queue.mutex);
            usleep(500 * 1000);  // Sleep 500ms
            continue;
        }
        
        // Copy task data while holding lock
        DownloadTask task;
        memcpy(&task, &st->download_queue.tasks[task_idx], sizeof(DownloadTask));
        
        pthread_mutex_unlock(&st->download_queue.mutex);
        
        // Build destination path
        char dest_dir[2048]; // Increased buffer size
        char dest_path[2560]; // Increased buffer size
        
        if (task.playlist_name[0]) {
            char safe_name[512];
            playlist_dir_name(task.playlist_name, safe_name, sizeof(safe_name));
            snprintf(dest_dir, sizeof(dest_dir), "%s/%s",
                     st->config.download_path, safe_name);
        } else {
            snprintf(dest_dir, sizeof(dest_dir), "%s", st->config.download_path);
        }
        
        // Create directory if needed
        mkdir_p(dest_dir);
        
        snprintf(dest_path, sizeof(dest_path), "%s/%s", dest_dir, task.sanitized_filename);
        
        // Check if file already exists (double-check)
        if (file_exists(dest_path)) {
            pthread_mutex_lock(&st->download_queue.mutex);
            st->download_queue.tasks[task_idx].status = DOWNLOAD_COMPLETED;
            st->download_queue.completed++;
            save_download_queue(st);
            pthread_mutex_unlock(&st->download_queue.mutex);
            continue;
        }
        
        // Build yt-dlp command (uses local binary if available + cookie args)
        char cookie_args[1200];
        build_cookie_args(st, cookie_args, sizeof(cookie_args));
        char cmd[4096];
        snprintf(cmd, sizeof(cmd),
                 "%s%s -x --audio-format mp3 --no-playlist --quiet --no-warnings "
                 "-o '%s' 'https://www.youtube.com/watch?v=%s' >/dev/null 2>&1",
                 get_ytdlp_cmd(st), cookie_args, dest_path, task.video_id);
        
        // Execute download
        int result = system(cmd);
        
        pthread_mutex_lock(&st->download_queue.mutex);
        
        if (result == 0 && file_exists(dest_path)) {
            st->download_queue.tasks[task_idx].status = DOWNLOAD_COMPLETED;
            st->download_queue.completed++;
        } else {
            st->download_queue.tasks[task_idx].status = DOWNLOAD_FAILED;
            st->download_queue.failed++;
        }
        
        save_download_queue(st);
        pthread_mutex_unlock(&st->download_queue.mutex);
    }
    
    return NULL;
}

static void start_download_thread(AppState *st) {
    if (st->download_queue.thread_running) return;
    
    st->download_queue.should_stop = false;
    
    if (pthread_create(&st->download_queue.thread, NULL, download_thread_func, st) == 0) {
        st->download_queue.thread_running = true;
    }
}

static void stop_download_thread(AppState *st) {
    if (!st->download_queue.thread_running) return;
    
    st->download_queue.should_stop = true;
    pthread_join(st->download_queue.thread, NULL);
    st->download_queue.thread_running = false;
}

// ============================================================================
// NEW: Download Queue Management
// ============================================================================

static int add_to_download_queue(AppState *st, const char *video_id, const char *title, 
                                  const char *playlist_name) {
    if (!video_id || !video_id[0]) return -1;
    
    // Build destination directory
    char dest_dir[2048]; // Increased buffer size
    if (playlist_name && playlist_name[0]) {
        char safe_name[512];
        playlist_dir_name(playlist_name, safe_name, sizeof(safe_name));
        snprintf(dest_dir, sizeof(dest_dir), "%s/%s",
                 st->config.download_path, safe_name);
    } else {
        snprintf(dest_dir, sizeof(dest_dir), "%s", st->config.download_path);
    }

    // Check if already downloaded
    if (file_exists_for_video(dest_dir, video_id)) {
        return 0;  // Already exists
    }
    
    pthread_mutex_lock(&st->download_queue.mutex);
    
    // Check if already in queue
    for (int i = 0; i < st->download_queue.count; i++) {
        if (strcmp(st->download_queue.tasks[i].video_id, video_id) == 0 &&
            st->download_queue.tasks[i].status == DOWNLOAD_PENDING) {
            pthread_mutex_unlock(&st->download_queue.mutex);
            return 0;  // Already queued
        }
    }
    
    // Check queue capacity
    if (st->download_queue.count >= MAX_DOWNLOAD_QUEUE) {
        pthread_mutex_unlock(&st->download_queue.mutex);
        return -1;
    }
    
    // Add new task
    DownloadTask *task = &st->download_queue.tasks[st->download_queue.count];
    
    strncpy(task->video_id, video_id, sizeof(task->video_id) - 1);
    task->video_id[sizeof(task->video_id) - 1] = '\0';
    
    strncpy(task->title, title ? title : "Unknown", sizeof(task->title) - 1);
    task->title[sizeof(task->title) - 1] = '\0';
    
    sanitize_title_for_filename(title, video_id, task->sanitized_filename, 
                                 sizeof(task->sanitized_filename));
    
    if (playlist_name) {
        strncpy(task->playlist_name, playlist_name, sizeof(task->playlist_name) - 1);
        task->playlist_name[sizeof(task->playlist_name) - 1] = '\0';
    } else {
        task->playlist_name[0] = '\0';
    }
    
    task->status = DOWNLOAD_PENDING;
    st->download_queue.count++;
    
    save_download_queue(st);
    
    pthread_mutex_unlock(&st->download_queue.mutex);
    
    // Start download thread if not running
    start_download_thread(st);
    
    return 1;  // Added to queue
}

static int get_pending_download_count(AppState *st) {
    pthread_mutex_lock(&st->download_queue.mutex);
    int count = 0;
    for (int i = 0; i < st->download_queue.count; i++) {
        if (st->download_queue.tasks[i].status == DOWNLOAD_PENDING ||
            st->download_queue.tasks[i].status == DOWNLOAD_ACTIVE) {
            count++;
        }
    }
    pthread_mutex_unlock(&st->download_queue.mutex);
    return count;
}

// ============================================================================
// Playlist Persistence
// ============================================================================

static void free_playlist_items(Playlist *pl) {
    for (int i = 0; i < pl->count; i++) {
        free(pl->items[i].title);
        free(pl->items[i].video_id);
        free(pl->items[i].url);
        pl->items[i].title = NULL;
        pl->items[i].video_id = NULL;
        pl->items[i].url = NULL;
    }
    pl->count = 0;
}

static void free_playlist(Playlist *pl) {
    free(pl->name);
    free(pl->filename);
    pl->name = NULL;
    pl->filename = NULL;
    if (pl->youtube_playlist_url) {
        free(pl->youtube_playlist_url);
        pl->youtube_playlist_url = NULL;
    }
    free_playlist_items(pl);
}

static void free_all_playlists(AppState *st) {
    for (int i = 0; i < st->playlist_count; i++) {
        free_playlist(&st->playlists[i]);
    }
    st->playlist_count = 0;
}

// Sanitize a name for use as a directory or file base name.
// Strips characters unsafe for paths (/, \, ', etc.), collapses spaces to _.
static void sanitize_name_for_path(const char *name, char *out, size_t out_size) {
    size_t j = 0;
    for (size_t i = 0; name[i] && j < out_size - 1; i++) {
        char c = name[i];
        if (isalnum((unsigned char)c) || c == '-' || c == '_') {
            out[j++] = tolower((unsigned char)c);
        } else if (c == ' ') {
            if (j > 0 && out[j-1] != '_') out[j++] = '_';
        }
        // everything else (/ \ ' " etc.) is silently dropped
    }
    // trim trailing underscores
    while (j > 0 && out[j-1] == '_') j--;
    out[j] = '\0';
    if (j == 0) { strncpy(out, "playlist", out_size - 1); out[out_size - 1] = '\0'; }
}

static char *sanitize_filename(const char *name) {
    size_t len = strlen(name);
    char *out = malloc(len + 6); // .json + null
    if (!out) return NULL;

    sanitize_name_for_path(name, out, len + 1);
    strcat(out, ".json");
    return out;
}

// Get the sanitized directory name for a playlist (for downloads).
static void playlist_dir_name(const char *playlist_name, char *out, size_t out_size) {
    sanitize_name_for_path(playlist_name, out, out_size);
}

static void save_playlists_index(AppState *st) {
    FILE *f = fopen(st->playlists_index, "w");
    if (!f) return;
    
    fprintf(f, "{\n  \"playlists\": [\n");
    
    for (int i = 0; i < st->playlist_count; i++) {
        char *escaped_name = json_escape_string(st->playlists[i].name);
        char *escaped_file = json_escape_string(st->playlists[i].filename);
        
        fprintf(f, "    {\"name\": \"%s\", \"filename\": \"%s\"}%s\n",
                escaped_name ? escaped_name : "",
                escaped_file ? escaped_file : "",
                (i < st->playlist_count - 1) ? "," : "");
        
        free(escaped_name);
        free(escaped_file);
    }
    
    fprintf(f, "  ]\n}\n");
    fclose(f);
}

static void save_playlist(AppState *st, int idx) {
    if (idx < 0 || idx >= st->playlist_count) return;
    
    Playlist *pl = &st->playlists[idx];
    
    char path[4096]; // Increased buffer size
    snprintf(path, sizeof(path), "%s/%s", st->playlists_dir, pl->filename);
    
    FILE *f = fopen(path, "w");
    if (!f) return;

    char *escaped_src = json_escape_string(pl->youtube_playlist_url);

    fprintf(f, "{\n  \"name\": \"%s\",\n  \"type\": \"%s\",\n  \"is_shared\": %s,\n  \"playlist_url\": \"%s\",\n  \"songs\": [\n",
            pl->name, pl->is_youtube_playlist ? "youtube" : "local",
            pl->is_shared ? "true" : "false",
            escaped_src ? escaped_src : "");

    free(escaped_src);
    
    for (int i = 0; i < pl->count; i++) {
        char *escaped_title = json_escape_string(pl->items[i].title);
        char *escaped_id = json_escape_string(pl->items[i].video_id);

        fprintf(f, "    {\"title\": \"%s\", \"video_id\": \"%s\", \"duration\": %d}%s\n",
                escaped_title ? escaped_title : "",
                escaped_id ? escaped_id : "",
                pl->items[i].duration,
                (i < pl->count - 1) ? "," : "");

        free(escaped_title);
        free(escaped_id);
    }
    
    fprintf(f, "  ]\n}\n");
    fclose(f);
}

static void load_playlist_songs(AppState *st, int idx) {
    if (idx < 0 || idx >= st->playlist_count) return;
    
    Playlist *pl = &st->playlists[idx];
    free_playlist_items(pl);
    
    char path[16384]; // Significantly increased buffer size
    snprintf(path, sizeof(path), "%s/%s", st->playlists_dir, pl->filename);
    
    FILE *f = fopen(path, "r");
    if (!f) return;
    
    // Read entire file
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    if (fsize <= 0 || fsize > 1024 * 1024) {
        fclose(f);
        return;
    }
    
    char *content = malloc(fsize + 1);
    if (!content) {
        fclose(f);
        return;
    }
    
    size_t read_size = fread(content, 1, fsize, f);
    content[read_size] = '\0';
    fclose(f);
    
    // Parse type
    char *type = json_get_string(content, "type");
    pl->is_youtube_playlist = (type && strcmp(type, "youtube") == 0);
    free(type);

    char *src = json_get_string(content, "playlist_url");
    if (src && src[0] && pl->is_youtube_playlist) {
        pl->youtube_playlist_url = src; // take ownership
    } else {
        free(src);
        pl->youtube_playlist_url = NULL;
    }

    // Parse is_shared flag
    char *shared_str = json_get_string(content, "is_shared");
    if (shared_str) {
        pl->is_shared = (strcmp(shared_str, "true") == 0);
        free(shared_str);
    } else {
        // Try boolean (json_get_string won't work for bool, check raw)
        const char *shared_pos = strstr(content, "\"is_shared\"");
        if (shared_pos) {
            shared_pos = strchr(shared_pos, ':');
            if (shared_pos) {
                shared_pos++;
                while (*shared_pos == ' ') shared_pos++;
                pl->is_shared = (strncmp(shared_pos, "true", 4) == 0);
            }
        }
    }
    
    // Parse songs array - simple approach
    const char *p = strstr(content, "\"songs\"");
    if (!p) {
        free(content);
        return;
    }
    
    p = strchr(p, '[');
    if (!p) {
        free(content);
        return;
    }
    
    // Find each song object
    while (pl->count < MAX_PLAYLIST_ITEMS) {
        const char *obj_start = strchr(p, '{');
        if (!obj_start) break;
        
        const char *obj_end = strchr(obj_start, '}');
        if (!obj_end) break;
        
        // Extract this object
        size_t obj_len = obj_end - obj_start + 1;
        char *obj = malloc(obj_len + 1);
        if (!obj) break;
        
        memcpy(obj, obj_start, obj_len);
        obj[obj_len] = '\0';
        
        char *title = json_get_string(obj, "title");
        char *video_id = json_get_string(obj, "video_id");
        int duration = json_get_int(obj, "duration", 0);

        if (title && video_id && video_id[0]) {
            pl->items[pl->count].title = title;
            pl->items[pl->count].video_id = video_id;

            char url[256];
            snprintf(url, sizeof(url), "https://www.youtube.com/watch?v=%s", video_id);
            pl->items[pl->count].url = strdup(url);
            pl->items[pl->count].duration = duration;
            
            pl->count++;
        } else {
            free(title);
            free(video_id);
        }
        
        free(obj);
        p = obj_end + 1;
    }
    
    free(content);
}

static void load_playlists(AppState *st) {
    free_all_playlists(st);
    
    FILE *f = fopen(st->playlists_index, "r");
    if (!f) return;
    
    // Read entire file
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    if (fsize <= 0 || fsize > 1024 * 1024) {
        fclose(f);
        return;
    }
    
    char *content = malloc(fsize + 1);
    if (!content) {
        fclose(f);
        return;
    }
    
    size_t read_size = fread(content, 1, fsize, f);
    content[read_size] = '\0';
    fclose(f);
    
    // Parse playlists array
    const char *p = strstr(content, "\"playlists\"");
    if (!p) {
        free(content);
        return;
    }
    
    p = strchr(p, '[');
    if (!p) {
        free(content);
        return;
    }
    
    while (st->playlist_count < MAX_PLAYLISTS) {
        const char *obj_start = strchr(p, '{');
        if (!obj_start) break;
        
        const char *obj_end = strchr(obj_start, '}');
        if (!obj_end) break;
        
        size_t obj_len = obj_end - obj_start + 1;
        char *obj = malloc(obj_len + 1);
        if (!obj) break;
        
        memcpy(obj, obj_start, obj_len);
        obj[obj_len] = '\0';
        
        char *name = json_get_string(obj, "name");
        char *filename = json_get_string(obj, "filename");
        
        if (name && filename && name[0] && filename[0]) {
            st->playlists[st->playlist_count].name = name;
            st->playlists[st->playlist_count].filename = filename;
            st->playlists[st->playlist_count].count = 0;
            st->playlist_count++;
        } else {
            free(name);
            free(filename);
        }
        
        free(obj);
        p = obj_end + 1;
    }
    
    free(content);
}

static int create_playlist(AppState *st, const char *name, bool is_youtube) {
    if (st->playlist_count >= MAX_PLAYLISTS) return -1;
    if (!name || !name[0]) return -1;
    
    // Check for duplicate name
    for (int i = 0; i < st->playlist_count; i++) {
        if (strcasecmp(st->playlists[i].name, name) == 0) {
            return -2; // Already exists
        }
    }
    
    char *filename = sanitize_filename(name);
    if (!filename) return -1;
    
    // Check for duplicate filename
    for (int i = 0; i < st->playlist_count; i++) {
        if (strcmp(st->playlists[i].filename, filename) == 0) {
            // Add number suffix
            char *new_filename = malloc(strlen(filename) + 10);
            if (!new_filename) {
                free(filename);
                return -1;
            }
            snprintf(new_filename, strlen(filename) + 10, "%d_%s", 
                     st->playlist_count, filename);
            free(filename);
            filename = new_filename;
            break;
        }
    }
    
    int idx = st->playlist_count;
    st->playlists[idx].name = strdup(name);
    st->playlists[idx].filename = filename;
    st->playlists[idx].count = 0;
    st->playlists[idx].is_youtube_playlist = is_youtube;
    st->playlists[idx].youtube_playlist_url = NULL;
    st->playlist_count++;
    
    save_playlists_index(st);
    save_playlist(st, idx);
    
    return idx;
}

static bool delete_playlist(AppState *st, int idx) {
    if (idx < 0 || idx >= st->playlist_count) return false;

    // Save playlist name before freeing (needed for directory deletion + sync)
    char playlist_name[256];
    strncpy(playlist_name, st->playlists[idx].name, sizeof(playlist_name) - 1);
    playlist_name[sizeof(playlist_name) - 1] = '\0';

    // Delete the playlist JSON file
    char path[16384];
    snprintf(path, sizeof(path), "%s/%s", st->playlists_dir, st->playlists[idx].filename);
    unlink(path);

    // Delete the download directory and all downloaded songs
    char download_dir[16384];
    char safe_dl_name[512];
    playlist_dir_name(playlist_name, safe_dl_name, sizeof(safe_dl_name));
    snprintf(download_dir, sizeof(download_dir), "%s/%s", st->config.download_path, safe_dl_name);
    if (dir_exists(download_dir)) {
        delete_directory_recursive(download_dir);
    }

    // Free memory
    free_playlist(&st->playlists[idx]);

    // Shift remaining playlists
    for (int i = idx; i < st->playlist_count - 1; i++) {
        st->playlists[i] = st->playlists[i + 1];
    }
    st->playlist_count--;

    // Clear the last slot
    memset(&st->playlists[st->playlist_count], 0, sizeof(Playlist));

    save_playlists_index(st);

    // Auto-delete from Surikata server (double-fork to avoid zombie)
    {
        sb_sync_config_t cfg = sb_load_config(st->config_dir);
        if (cfg.enabled && strlen(cfg.token) > 0) {
            pid_t pid = fork();
            if (pid == 0) {
                pid_t pid2 = fork();
                if (pid2 == 0) {
                    sb_sync_init();
                    sb_delete_playlist(&cfg, playlist_name);
                    sb_sync_cleanup();
                    _exit(0);
                }
                _exit(0);
            } else if (pid > 0) {
                waitpid(pid, NULL, 0);
            }
        }
    }

    return true;
}

static bool add_song_to_playlist(AppState *st, int playlist_idx, Song *song) {
    if (playlist_idx < 0 || playlist_idx >= st->playlist_count) return false;
    if (!song || !song->video_id) return false;
    
    Playlist *pl = &st->playlists[playlist_idx];
    
    // Load songs if not loaded
    if (pl->count == 0 && file_exists(st->playlists_dir)) {
        load_playlist_songs(st, playlist_idx);
    }
    
    if (pl->count >= MAX_PLAYLIST_ITEMS) return false;
    
    // Check for duplicate
    for (int i = 0; i < pl->count; i++) {
        if (pl->items[i].video_id && strcmp(pl->items[i].video_id, song->video_id) == 0) {
            return false; // Already in playlist
        }
    }
    
    int idx = pl->count;
    pl->items[idx].title = song->title ? strdup(song->title) : strdup("Unknown");
    pl->items[idx].video_id = strdup(song->video_id);

    char url[256];
    snprintf(url, sizeof(url), "https://www.youtube.com/watch?v=%s", song->video_id);
    pl->items[idx].url = strdup(url);
    pl->items[idx].duration = song->duration;

    pl->count++;

    save_playlist(st, playlist_idx);

    // Automatically queue song for download
    add_to_download_queue(st, song->video_id, song->title, pl->name);

    // Auto-sync to Surikata
    auto_sync_playlist(st, playlist_idx);

    return true;
}

static bool remove_song_from_playlist(AppState *st, int playlist_idx, int song_idx) {
    if (playlist_idx < 0 || playlist_idx >= st->playlist_count) return false;
    
    Playlist *pl = &st->playlists[playlist_idx];
    if (song_idx < 0 || song_idx >= pl->count) return false;
    
    // Free song data
    free(pl->items[song_idx].title);
    free(pl->items[song_idx].video_id);
    free(pl->items[song_idx].url);
    
    // Shift remaining songs
    for (int i = song_idx; i < pl->count - 1; i++) {
        pl->items[i] = pl->items[i + 1];
    }
    pl->count--;

    // Clear last slot
    memset(&pl->items[pl->count], 0, sizeof(Song));

    save_playlist(st, playlist_idx);

    // Auto-sync to Surikata
    auto_sync_playlist(st, playlist_idx);

    return true;
}

// ============================================================================
// MPV IPC Communication
// ============================================================================

static void mpv_disconnect(void) {
    if (mpv_ipc_fd >= 0) {
        sb_log("[PLAYBACK] mpv_disconnect: closing IPC fd=%d", mpv_ipc_fd);
        close(mpv_ipc_fd);
        mpv_ipc_fd = -1;
    }
}

static bool mpv_connect(void) {
    if (mpv_ipc_fd >= 0) {
        sb_log("[PLAYBACK] mpv_connect: already connected (fd=%d)", mpv_ipc_fd);
        return true;
    }
    if (!file_exists(IPC_SOCKET)) {
        sb_log("[PLAYBACK] mpv_connect: IPC socket %s does not exist", IPC_SOCKET);
        return false;
    }

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        sb_log("[PLAYBACK] mpv_connect: socket() failed: %s (errno=%d)", strerror(errno), errno);
        return false;
    }

    // Set non-blocking
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, IPC_SOCKET, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        sb_log("[PLAYBACK] mpv_connect: connect() to %s failed: %s (errno=%d)", IPC_SOCKET, strerror(errno), errno);
        close(fd);
        return false;
    }

    mpv_ipc_fd = fd;
    sb_log("[PLAYBACK] mpv_connect: connected to mpv IPC socket (fd=%d)", fd);

    // Enable end-file event observation
    const char *observe_cmd = "{\"command\":[\"observe_property\",1,\"eof-reached\"]}\n";
    ssize_t w = write(mpv_ipc_fd, observe_cmd, strlen(observe_cmd));
    if (w < 0) {
        sb_log("[PLAYBACK] mpv_connect: failed to send observe command: %s", strerror(errno));
    }
    (void)w;

    return true;
}

static void mpv_send_command(const char *cmd) {
    sb_log("[PLAYBACK] mpv_send_command: sending: %s", cmd);
    if (!mpv_connect()) {
        sb_log("[PLAYBACK] mpv_send_command: persistent connection failed, trying one-shot");
        // Try one-shot connection
        int fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) {
            sb_log("[PLAYBACK] mpv_send_command: one-shot socket() failed: %s", strerror(errno));
            return;
        }

        struct sockaddr_un addr;
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, IPC_SOCKET, sizeof(addr.sun_path) - 1);

        if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
            ssize_t w = write(fd, cmd, strlen(cmd));
            w = write(fd, "\n", 1);
            sb_log("[PLAYBACK] mpv_send_command: one-shot command sent (bytes=%zd)", w);
            (void)w;
        } else {
            sb_log("[PLAYBACK] mpv_send_command: one-shot connect() failed: %s", strerror(errno));
        }
        close(fd);
        return;
    }

    ssize_t w = write(mpv_ipc_fd, cmd, strlen(cmd));
    if (w < 0) {
        sb_log("[PLAYBACK] mpv_send_command: write failed: %s (errno=%d)", strerror(errno), errno);
    } else {
        sb_log("[PLAYBACK] mpv_send_command: sent %zd bytes on fd=%d", w, mpv_ipc_fd);
    }
    w = write(mpv_ipc_fd, "\n", 1);
    (void)w;
}

static void mpv_toggle_pause(void) {
    sb_log("[PLAYBACK] mpv_toggle_pause called");
    mpv_send_command("{\"command\":[\"cycle\",\"pause\"]}");
}

static void mpv_stop_playback(void) {
    sb_log("[PLAYBACK] mpv_stop_playback called");
    mpv_send_command("{\"command\":[\"stop\"]}");
}

static void mpv_seek(int seconds) {
    sb_log("[PLAYBACK] mpv_seek: seeking %+d seconds", seconds);
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "{\"command\":[\"seek\",\"%d\",\"relative\"]}", seconds);
    mpv_send_command(cmd);
}

static void mpv_seek_absolute(int seconds) {
    sb_log("[PLAYBACK] mpv_seek_absolute: seeking to %d seconds", seconds);
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "{\"command\":[\"seek\",\"%d\",\"absolute\"]}", seconds);
    mpv_send_command(cmd);
}

// Sync cookie options and audio-only format into the running mpv process via IPC.
// Needed because --ytdl-raw-options is read at mpv startup; if cookies are
// configured later we must push them at runtime before each loadfile.
// Also forces ytdl-format=bestaudio/best so yt-dlp picks a stream mpv can play.
//
// Strategy:
//  - With JS runtime available (deno/node/bun) → use defaults: cookies + format
//    only. yt-dlp picks the best client and resolves YouTube challenges.
//  - WITHOUT JS runtime → fall back to extractor-args=player_client=web_safari
//    which doesn't require JS challenge solving (less reliable but works).
static void mpv_apply_cookie_options(AppState *st) {
    char value[1024];
    value[0] = '\0';
    size_t pos = 0;

    if (st->config.yt_cookies_mode == YT_COOKIES_AUTO && st->config.yt_cookies_browser[0]) {
        pos += snprintf(value + pos, sizeof(value) - pos,
                        "cookies-from-browser=%s",
                        st->config.yt_cookies_browser);
    } else if (st->config.yt_cookies_mode == YT_COOKIES_MANUAL && st->config.yt_cookies_file[0]) {
        pos += snprintf(value + pos, sizeof(value) - pos,
                        "cookies=%s",
                        st->config.yt_cookies_file);
    }

    // If no JS runtime, add extractor-args fallback (less reliable but no JS needed)
    if (!js_runtime_already_available(st)) {
        if (pos > 0) {
            pos += snprintf(value + pos, sizeof(value) - pos, ",");
        }
        snprintf(value + pos, sizeof(value) - pos,
                 "extractor-args=youtube:player_client=web_safari");
    }

    char cmd[1500];
    snprintf(cmd, sizeof(cmd),
             "{\"command\":[\"set_property\",\"ytdl-raw-options\",\"%s\"]}",
             value);
    sb_log("[PLAYBACK] mpv_apply_cookie_options: %s", value[0] ? value : "(empty)");
    mpv_send_command(cmd);

    // Force a single audio file format (no manifest/HLS that mpv treats as playlist).
    // Prefer m4a/webm pure audio, fallback to bestaudio.
    mpv_send_command("{\"command\":[\"set_property\",\"ytdl-format\","
                     "\"bestaudio[ext=m4a]/bestaudio[ext=webm]/bestaudio\"]}");
}

static void mpv_load_url(const char *url) {
    sb_log("[PLAYBACK] mpv_load_url: loading URL: %s", url);

    char *escaped = NULL;
    size_t n = 0;
    FILE *mem = open_memstream(&escaped, &n);
    if (!mem) {
        sb_log("[PLAYBACK] mpv_load_url: open_memstream failed: %s", strerror(errno));
        return;
    }

    fputc('"', mem);
    for (const char *p = url; *p; p++) {
        if (*p == '"' || *p == '\\') fputc('\\', mem);
        fputc(*p, mem);
    }
    fputc('"', mem);
    fclose(mem);

    char cmd[4096];
    snprintf(cmd, sizeof(cmd),
             "{\"command\":[\"loadfile\",%s,\"replace\"]}", escaped);
    free(escaped);

    sb_log("[PLAYBACK] mpv_load_url: sending loadfile command to mpv");
    mpv_send_command(cmd);

    // Ensure mpv is unpaused — pause property persists across loadfile
    mpv_send_command("{\"command\":[\"set_property\",\"pause\",false]}");
}

static void mpv_start_if_needed(AppState *st) {
    sb_log("[PLAYBACK] mpv_start_if_needed: checking if mpv is running...");
    if (file_exists(IPC_SOCKET) && mpv_connect()) {
        sb_log("[PLAYBACK] mpv_start_if_needed: mpv already running and connected");
        return;
    }

    sb_log("[PLAYBACK] mpv_start_if_needed: mpv not running, starting new instance...");
    unlink(IPC_SOCKET);
    mpv_disconnect();

    // Build ytdl_hook path option so mpv can find yt-dlp
    const char *ytdlp_path = get_ytdlp_cmd(st);
    char ytdl_opt[1200];
    snprintf(ytdl_opt, sizeof(ytdl_opt), "--script-opts=ytdl_hook-ytdl_path=%s", ytdlp_path);
    sb_log("[PLAYBACK] mpv_start_if_needed: yt-dlp path for mpv: %s", ytdlp_path);

    // Build cookies option for mpv (passed to yt-dlp via ytdl-raw-options)
    char ytdl_cookie_opt[1200];
    ytdl_cookie_opt[0] = '\0';
    if (st->config.yt_cookies_mode == YT_COOKIES_AUTO && st->config.yt_cookies_browser[0]) {
        snprintf(ytdl_cookie_opt, sizeof(ytdl_cookie_opt),
                 "--ytdl-raw-options=cookies-from-browser=%s",
                 st->config.yt_cookies_browser);
    } else if (st->config.yt_cookies_mode == YT_COOKIES_MANUAL && st->config.yt_cookies_file[0]) {
        snprintf(ytdl_cookie_opt, sizeof(ytdl_cookie_opt),
                 "--ytdl-raw-options=cookies=%s",
                 st->config.yt_cookies_file);
    }
    bool has_cookie_opt = (ytdl_cookie_opt[0] != '\0');

    pid_t pid = fork();
    if (pid == 0) {
        int fd = open("/dev/null", O_WRONLY);
        if (fd >= 0) {
            dup2(fd, STDOUT_FILENO);
            dup2(fd, STDERR_FILENO);
            close(fd);
        }
        // --ytdl-format=bestaudio ensures yt-dlp picks an audio stream that
        // mpv can play (default selector may pick storyboard/HLS that mpv
        // can't handle, causing "unrecognized file format" errors).
        if (has_cookie_opt) {
            execlp("mpv", "mpv",
                   "--no-video",
                   "--idle=yes",
                   "--force-window=no",
                   "--really-quiet",
                   "--input-ipc-server=" IPC_SOCKET,
                   "--ytdl-format=bestaudio[ext=m4a]/bestaudio[ext=webm]/bestaudio",
                   ytdl_opt,
                   ytdl_cookie_opt,
                   (char *)NULL);
        } else {
            execlp("mpv", "mpv",
                   "--no-video",
                   "--idle=yes",
                   "--force-window=no",
                   "--really-quiet",
                   "--input-ipc-server=" IPC_SOCKET,
                   "--ytdl-format=bestaudio[ext=m4a]/bestaudio[ext=webm]/bestaudio",
                   ytdl_opt,
                   (char *)NULL);
        }
        _exit(127);
    }

    if (pid < 0) {
        sb_log("[PLAYBACK] mpv_start_if_needed: fork() failed: %s (errno=%d)", strerror(errno), errno);
        return;
    }

    sb_log("[PLAYBACK] mpv_start_if_needed: mpv forked with pid=%d, waiting for IPC socket...", pid);
    mpv_pid = pid;
    bool connected = false;
    for (int i = 0; i < 100; i++) {
        if (file_exists(IPC_SOCKET)) {
            sb_log("[PLAYBACK] mpv_start_if_needed: IPC socket appeared after %d ms", (i + 1) * 50);
            usleep(50 * 1000);
            if (mpv_connect()) {
                sb_log("[PLAYBACK] mpv_start_if_needed: successfully connected to mpv (pid=%d)", pid);
                connected = true;
            } else {
                sb_log("[PLAYBACK] mpv_start_if_needed: IPC socket exists but connect failed");
            }
            break;
        }
        usleep(50 * 1000);
    }
    if (!connected) {
        sb_log("[PLAYBACK] mpv_start_if_needed: WARNING - failed to connect after 5s timeout (pid=%d)", pid);
    }
}

static void mpv_quit(void) {
    sb_log("[PLAYBACK] mpv_quit: shutting down mpv (pid=%d)", mpv_pid);
    mpv_send_command("{\"command\":[\"quit\"]}");
    usleep(100 * 1000);

    mpv_disconnect();

    if (mpv_pid > 0) {
        kill(mpv_pid, SIGTERM);
        waitpid(mpv_pid, NULL, WNOHANG);
        sb_log("[PLAYBACK] mpv_quit: sent SIGTERM to pid=%d", mpv_pid);
        mpv_pid = -1;
    }
    unlink(IPC_SOCKET);
    sb_log("[PLAYBACK] mpv_quit: cleanup complete");
}

// Check if mpv finished playing (returns true if track ended)
// Only returns true for genuine end-of-file, not loading states
static bool mpv_check_track_end(void) {
    if (mpv_ipc_fd < 0) return false;

    char buf[4096];
    ssize_t n = read(mpv_ipc_fd, buf, sizeof(buf) - 1);

    if (n <= 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            // Connection lost
            sb_log("[PLAYBACK] mpv_check_track_end: connection lost: %s (errno=%d)", strerror(errno), errno);
            mpv_disconnect();
        }
        return false;
    }

    buf[n] = '\0';
    sb_log("[PLAYBACK] mpv_check_track_end: IPC data received (%zd bytes): %.200s", n, buf);

    // Check for end-file event with reason "eof"
    // Format: {"event":"end-file","reason":"eof",...}
    if (strstr(buf, "\"event\":\"end-file\"") && strstr(buf, "\"reason\":\"eof\"")) {
        sb_log("[PLAYBACK] mpv_check_track_end: track ended (end-file EOF)");
        return true;
    }

    // Check for eof-reached property change (observed via observe_property)
    // Format: {"event":"property-change","name":"eof-reached","data":true}
    if (strstr(buf, "\"eof-reached\"") && strstr(buf, "\"data\":true")) {
        sb_log("[PLAYBACK] mpv_check_track_end: track ended (eof-reached property)");
        return true;
    }

    // Also treat end-file with error as track end (skip to next instead of hanging)
    if (strstr(buf, "\"event\":\"end-file\"") && strstr(buf, "\"reason\":\"error\"")) {
        sb_log("[PLAYBACK] mpv_check_track_end: track ended with ERROR, advancing");
        return true;
    }

    return false;
}

// ============================================================================
// Search Functions
// ============================================================================

static void free_search_results(AppState *st) {
    for (int i = 0; i < st->search_count; i++) {
        free(st->search_results[i].title);
        free(st->search_results[i].video_id);
        free(st->search_results[i].url);
        st->search_results[i].title = NULL;
        st->search_results[i].video_id = NULL;
        st->search_results[i].url = NULL;
    }
    st->search_count = 0;
    st->search_selected = 0;
    st->search_scroll = 0;
}

static int run_search(AppState *st, const char *raw_query) {
    free_search_results(st);

    char query_buf[256];
    strncpy(query_buf, raw_query, sizeof(query_buf) - 1);
    query_buf[sizeof(query_buf) - 1] = '\0';
    char *query = trim_whitespace(query_buf);

    if (!query[0]) return 0;

    sb_log("[PLAYBACK] run_search: query=\"%s\"", query);

    // Escape for shell
    char escaped_query[512];
    size_t j = 0;
    for (size_t i = 0; query[i] && j < sizeof(escaped_query) - 5; i++) {
        char c = query[i];
        if (c == '"' || c == '\\' || c == '$' || c == '`') {
            escaped_query[j++] = '\\';
        }
        escaped_query[j++] = c;
    }
    escaped_query[j] = '\0';

    char cookie_args[1200];
    build_cookie_args(st, cookie_args, sizeof(cookie_args));
    char cmd[2048];
    // Merge stderr so we can detect "Sign in to confirm" YouTube bot block.
    snprintf(cmd, sizeof(cmd),
             "%s%s --flat-playlist --quiet --no-warnings "
             "--print '%%(title)s|||%%(id)s|||%%(duration)s' "
             "\"ytsearch%d:%s\" 2>&1",
             get_ytdlp_cmd(st), cookie_args, st->config.max_results, escaped_query);

    sb_log("[PLAYBACK] run_search: executing: %s", cmd);

    FILE *fp = popen(cmd, "r");
    if (!fp) {
        sb_log("[PLAYBACK] run_search: popen() failed: %s", strerror(errno));
        return -1;
    }

    char *line = NULL;
    size_t cap = 0;
    int count = 0;
    st->yt_blocked = false;

    while (count < st->config.max_results && getline(&line, &cap, fp) != -1) {
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) {
            line[--len] = '\0';
        }
        // Detect YouTube bot check
        if (strstr(line, "Sign in to confirm")) {
            st->yt_blocked = true;
            continue;
        }
        
        if (!line[0]) continue;
        if (strncmp(line, "ERROR", 5) == 0) continue;
        if (strncmp(line, "WARNING", 7) == 0) continue;
        
        char *sep1 = strstr(line, "|||");
        if (!sep1) continue;
        *sep1 = '\0';

        char *sep2 = strstr(sep1 + 3, "|||");
        if (!sep2) continue;
        *sep2 = '\0';

        const char *title = line;
        const char *video_id = sep1 + 3;
        const char *duration_str = sep2 + 3;
        if (!video_id[0]) continue;

        size_t id_len = strlen(video_id);
        if (id_len < 5 || id_len > 20) continue;

        st->search_results[count].title = strdup(title);
        st->search_results[count].video_id = strdup(video_id);

        char fullurl[256];
        snprintf(fullurl, sizeof(fullurl),
                 "https://www.youtube.com/watch?v=%s", video_id);
        st->search_results[count].url = strdup(fullurl);
        st->search_results[count].duration = atoi(duration_str);
        
        if (st->search_results[count].title && 
            st->search_results[count].video_id &&
            st->search_results[count].url) {
            count++;
        } else {
            free(st->search_results[count].title);
            free(st->search_results[count].video_id);
            free(st->search_results[count].url);
        }
    }
    
    free(line);
    pclose(fp);
    
    st->search_count = count;
    st->search_selected = 0;
    st->search_scroll = 0;
    strncpy(st->query, query, sizeof(st->query) - 1);
    st->query[sizeof(st->query) - 1] = '\0';

    sb_log("[PLAYBACK] run_search: found %d results for query=\"%s\"", count, query);

    return count;
}

// ============================================================================
// Playback Functions
// ============================================================================

static void play_search_result(AppState *st, int idx) {
    if (idx < 0 || idx >= st->search_count) {
        sb_log("[PLAYBACK] play_search_result: invalid index %d (count=%d)", idx, st->search_count);
        return;
    }
    if (!st->search_results[idx].url) {
        sb_log("[PLAYBACK] play_search_result: no URL for result %d", idx);
        return;
    }

    sb_log("[PLAYBACK] play_search_result: playing result #%d: \"%s\" url=%s",
           idx, st->search_results[idx].title ? st->search_results[idx].title : "(null)",
           st->search_results[idx].url);

    mpv_start_if_needed(st);
    mpv_apply_cookie_options(st);
    mpv_load_url(st->search_results[idx].url);

    st->playing_index = idx;
    st->playing_from_playlist = false;
    st->playing_playlist_idx = -1;
    st->paused = false;
    st->playback_started = time(NULL);
    sb_log("[PLAYBACK] play_search_result: playback started for result #%d", idx);
}

static void play_playlist_song(AppState *st, int playlist_idx, int song_idx) {
    if (playlist_idx < 0 || playlist_idx >= st->playlist_count) {
        sb_log("[PLAYBACK] play_playlist_song: invalid playlist_idx=%d (count=%d)", playlist_idx, st->playlist_count);
        return;
    }

    Playlist *pl = &st->playlists[playlist_idx];
    if (song_idx < 0 || song_idx >= pl->count) {
        sb_log("[PLAYBACK] play_playlist_song: invalid song_idx=%d (count=%d) in playlist \"%s\"",
               song_idx, pl->count, pl->name ? pl->name : "(null)");
        return;
    }
    if (!pl->items[song_idx].url) {
        sb_log("[PLAYBACK] play_playlist_song: no URL for song %d in playlist \"%s\"",
               song_idx, pl->name ? pl->name : "(null)");
        return;
    }

    sb_log("[PLAYBACK] play_playlist_song: playlist=\"%s\" song=#%d \"%s\" video_id=%s url=%s is_youtube=%d",
           pl->name ? pl->name : "(null)", song_idx,
           pl->items[song_idx].title ? pl->items[song_idx].title : "(null)",
           pl->items[song_idx].video_id ? pl->items[song_idx].video_id : "(null)",
           pl->items[song_idx].url,
           pl->is_youtube_playlist);

    mpv_start_if_needed(st);
    mpv_apply_cookie_options(st);

    // Check if YouTube playlist - always stream
    if (pl->is_youtube_playlist) {
        sb_log("[PLAYBACK] play_playlist_song: streaming YouTube playlist song: %s", pl->items[song_idx].url);
        mpv_load_url(pl->items[song_idx].url);
    } else {
        // Check if local file exists for this song
        char local_path[2048];
        if (get_local_file_path_for_song(st, pl->name, pl->items[song_idx].video_id,
                                          local_path, sizeof(local_path))) {
            // Play from local file
            sb_log("[PLAYBACK] play_playlist_song: playing LOCAL file: %s", local_path);
            mpv_load_url(local_path);
        } else {
            // Stream from YouTube
            sb_log("[PLAYBACK] play_playlist_song: no local file, STREAMING from: %s", pl->items[song_idx].url);
            mpv_load_url(pl->items[song_idx].url);
        }
    }

    st->playing_index = song_idx;
    st->playing_from_playlist = true;
    st->playing_playlist_idx = playlist_idx;
    st->paused = false;
    st->playback_started = time(NULL);
    sb_log("[PLAYBACK] play_playlist_song: playback started");
}

static int get_random_index(int count, int current) {
    if (count <= 1) return 0;
    int next;
    do {
        next = rand() % count;
    } while (next == current && count > 1);
    return next;
}

static void play_next(AppState *st) {
    sb_log("[PLAYBACK] play_next: current index=%d, from_playlist=%d, playlist_idx=%d, shuffle=%d",
           st->playing_index, st->playing_from_playlist, st->playing_playlist_idx, st->shuffle_mode);
    RepeatMode effective_repeat = st->repeat_mode;
    if (!st->playing_from_playlist && effective_repeat == REPEAT_ALL) {
        effective_repeat = REPEAT_OFF;
    }

    if (effective_repeat == REPEAT_ONE && st->playing_index >= 0) {
        sb_log("[PLAYBACK] play_next: repeat-one mode, replaying current track");
        if (st->playing_from_playlist && st->playing_playlist_idx >= 0) {
            play_playlist_song(st, st->playing_playlist_idx, st->playing_index);
            st->playlist_song_selected = st->playing_index;
        } else {
            play_search_result(st, st->playing_index);
            st->search_selected = st->playing_index;
        }
        return;
    }

    if (st->playing_from_playlist && st->playing_playlist_idx >= 0) {
        Playlist *pl = &st->playlists[st->playing_playlist_idx];
        int next;
        if (st->shuffle_mode) {
            next = get_random_index(pl->count, st->playing_index);
            sb_log("[PLAYBACK] play_next: shuffle mode, random index=%d/%d", next, pl->count);
        } else {
            next = st->playing_index + 1;
        }
        if (next < pl->count) {
            sb_log("[PLAYBACK] play_next: advancing to playlist song #%d/%d", next, pl->count);
            play_playlist_song(st, st->playing_playlist_idx, next);
            st->playlist_song_selected = next;
        } else if (effective_repeat == REPEAT_ALL && pl->count > 0) {
            sb_log("[PLAYBACK] play_next: repeat-all mode, wrapping playlist to start");
            play_playlist_song(st, st->playing_playlist_idx, 0);
            st->playlist_song_selected = 0;
        } else if (st->shuffle_mode) {
            // In shuffle mode, loop back
            next = get_random_index(pl->count, st->playing_index);
            sb_log("[PLAYBACK] play_next: shuffle loop, random index=%d/%d", next, pl->count);
            play_playlist_song(st, st->playing_playlist_idx, next);
            st->playlist_song_selected = next;
        } else {
            sb_log("[PLAYBACK] play_next: already at last song in playlist (%d/%d)", st->playing_index, pl->count);
            st->playing_index = -1;
            st->playing_from_playlist = false;
            st->playing_playlist_idx = -1;
            st->paused = false;
        }
    } else if (st->search_count > 0) {
        int next;
        if (st->shuffle_mode) {
            next = get_random_index(st->search_count, st->playing_index);
            sb_log("[PLAYBACK] play_next: shuffle mode, random search index=%d/%d", next, st->search_count);
        } else {
            next = st->playing_index + 1;
        }
        if (next < st->search_count) {
            sb_log("[PLAYBACK] play_next: advancing to search result #%d/%d", next, st->search_count);
            play_search_result(st, next);
            st->search_selected = next;
        } else if (st->shuffle_mode) {
            // In shuffle mode, loop back
            next = get_random_index(st->search_count, st->playing_index);
            sb_log("[PLAYBACK] play_next: shuffle loop, random search index=%d/%d", next, st->search_count);
            play_search_result(st, next);
            st->search_selected = next;
        } else {
            sb_log("[PLAYBACK] play_next: already at last search result (%d/%d)", st->playing_index, st->search_count);
            st->playing_index = -1;
            st->playing_from_playlist = false;
            st->playing_playlist_idx = -1;
            st->paused = false;
        }
    }
}

static void play_prev(AppState *st) {
    sb_log("[PLAYBACK] play_prev: current index=%d, from_playlist=%d, playlist_idx=%d",
           st->playing_index, st->playing_from_playlist, st->playing_playlist_idx);
    if (st->playing_from_playlist && st->playing_playlist_idx >= 0) {
        int prev = st->playing_index - 1;
        if (prev >= 0) {
            sb_log("[PLAYBACK] play_prev: going back to playlist song #%d", prev);
            play_playlist_song(st, st->playing_playlist_idx, prev);
            st->playlist_song_selected = prev;
        } else {
            sb_log("[PLAYBACK] play_prev: already at first song in playlist");
        }
    } else if (st->search_count > 0) {
        int prev = st->playing_index - 1;
        if (prev >= 0) {
            sb_log("[PLAYBACK] play_prev: going back to search result #%d", prev);
            play_search_result(st, prev);
            st->search_selected = prev;
        } else {
            sb_log("[PLAYBACK] play_prev: already at first search result");
        }
    }
}

// ============================================================================
// UI Drawing
// ============================================================================

static void format_duration(int sec, char out[16]) {
    if (sec <= 0) {
        snprintf(out, 16, "--:--");
        return;
    }
    int h = sec / 3600;
    int m = (sec % 3600) / 60;
    int s = sec % 60;
    if (h > 0) {
        snprintf(out, 16, "%d:%02d:%02d", h, m, s);
    } else {
        snprintf(out, 16, "%02d:%02d", m, s);
    }
}

// NEW: Updated draw_header to include VIEW_SETTINGS
// Compare version strings like "0.6" vs "0.7" (locale-independent)
// Returns: -1 if a < b, 0 if equal, 1 if a > b
static int version_compare(const char *a, const char *b) {
    int a_major = 0, a_minor = 0, b_major = 0, b_minor = 0;
    sscanf(a, "%d.%d", &a_major, &a_minor);
    sscanf(b, "%d.%d", &b_major, &b_minor);
    if (a_major != b_major) return a_major < b_major ? -1 : 1;
    if (a_minor != b_minor) return a_minor < b_minor ? -1 : 1;
    return 0;
}

static void draw_header(AppState *st, int cols, ViewMode view) {
    // Line 1: Title
    attron(A_BOLD);
    mvprintw(0, 0, " ShellBeats v%s ", SHELLBEATS_VERSION);
    attroff(A_BOLD);

    // Update notification (static) on the right side of line 0
    if (st->latest_version[0] != '\0') {
        if (version_compare(SHELLBEATS_VERSION, st->latest_version) < 0) {
            char update_msg[64];
            snprintf(update_msg, sizeof(update_msg), "Update to v%s", st->latest_version);
            int msg_x = cols - (int)strlen(update_msg) - 1;
            if (msg_x > 20) {
                attron(A_BOLD);
                mvprintw(0, msg_x, "%s", update_msg);
                attroff(A_BOLD);
            }
        }
    }

    // Line 2-3: Shortcuts (two lines)
    switch (view) {
        case VIEW_SEARCH:
            mvprintw(1, 0, "  /,s: search | Enter: play | Space: pause | n/p: next/prev | R: shuffle | L: repeat | t: jump");
            mvprintw(2, 0, "  Left/Right: seek | a: add | d: download | f: playlists | S: settings | q: quit");
            break;
        case VIEW_PLAYLISTS:
            mvprintw(1, 0, "  Enter: open | c: create | e: rename | p: add YouTube | x: delete | d: download all");
            mvprintw(2, 0, "  s: surisync | Esc: back | i: about | q: quit");
            break;
        case VIEW_PLAYLIST_SONGS:
            mvprintw(1, 0, "  Enter: play | Space: pause | n/p: next/prev | R: shuffle | t: jump | Left/Right: seek");
            mvprintw(2, 0, "  a: add | d: download | X: remove | D: download all | u: sync YT | s: surisync | Esc: back");
            break;
        case VIEW_ADD_TO_PLAYLIST:
            mvprintw(1, 0, "  Enter: add to playlist | c: create new playlist");
            mvprintw(2, 0, "  Esc: cancel");
            break;
        case VIEW_SETTINGS:
            mvprintw(1, 0, "  Up/Down: navigate | Enter: edit/toggle");
            mvprintw(2, 0, "  Esc: back | i: about | q: quit");
            break;
        case VIEW_ABOUT:
            mvprintw(1, 0, "  Press any key to close");
            move(2, 0);
            break;
        case VIEW_SURISYNC:
            mvprintw(1, 0, "  Up/Down: navigate | Enter: select/toggle");
            mvprintw(2, 0, "  Esc: close");
            break;
    }

    mvhline(3, 0, ACS_HLINE, cols);
}

// NEW: Get spinner character for download animation
static char get_spinner_char(int frame) {
    const char spinner[] = {'|', '/', '-', '\\'};
    return spinner[frame % 4];
}

// NEW: Draw download status in status bar area
static void draw_download_status(AppState *st, int rows, int cols) {
    char dl_status[128] = {0};
    char spinner = get_spinner_char(st->spinner_frame);
    int status_parts = 0;

    // yt-dlp update status (shown while updating)
    if (st->ytdlp_updating) {
        snprintf(dl_status, sizeof(dl_status), "[%c Fetching updates...]", spinner);
        status_parts++;
    }

    // deno install status (shown while installing JS runtime)
    if (st->deno_installing) {
        char prev[512];
        snprintf(prev, sizeof(prev), "%s", dl_status);
        snprintf(dl_status, sizeof(dl_status), "%s%s[%c %s]",
                 prev, prev[0] ? " " : "",
                 spinner,
                 st->deno_install_status[0] ? st->deno_install_status : "Installing deno...");
        status_parts++;
    }

    // Download queue status
    pthread_mutex_lock(&st->download_queue.mutex);

    int pending_count = 0;
    int completed = st->download_queue.completed;
    int failed = st->download_queue.failed;

    for (int i = 0; i < st->download_queue.count; i++) {
        if (st->download_queue.tasks[i].status == DOWNLOAD_PENDING ||
            st->download_queue.tasks[i].status == DOWNLOAD_ACTIVE) {
            pending_count++;
        }
    }

    pthread_mutex_unlock(&st->download_queue.mutex);

    if (pending_count > 0) {
        char queue_status[64];
        if (failed > 0) {
            snprintf(queue_status, sizeof(queue_status), "[%c %d/%d %d!]",
                     spinner, completed, completed + pending_count, failed);
        } else {
            snprintf(queue_status, sizeof(queue_status), "[%c %d/%d]",
                     spinner, completed, completed + pending_count);
        }
        if (status_parts > 0) {
            // Append after update status
            size_t cur_len = strlen(dl_status);
            snprintf(dl_status + cur_len, sizeof(dl_status) - cur_len, " %s", queue_status);
        } else {
            snprintf(dl_status, sizeof(dl_status), "%s", queue_status);
        }
        status_parts++;
    }

    if (status_parts == 0) return;

    int x = cols - (int)strlen(dl_status) - 1;
    if (x > 0) {
        mvprintw(rows - 1, x, "%s", dl_status);
    }
}

static void draw_now_playing(AppState *st, int rows, int cols) {
    mvhline(rows - 2, 0, ACS_HLINE, cols);
    
    const char *title = NULL;
    
    if (st->playing_from_playlist && st->playing_playlist_idx >= 0 &&
        st->playing_playlist_idx < st->playlist_count) {
        Playlist *pl = &st->playlists[st->playing_playlist_idx];
        if (st->playing_index >= 0 && st->playing_index < pl->count) {
            title = pl->items[st->playing_index].title;
        }
    } else if (st->playing_index >= 0 && st->playing_index < st->search_count) {
        title = st->search_results[st->playing_index].title;
    }
    
    if (title) {
        mvprintw(rows - 1, 0, " Now playing: ");
        attron(A_BOLD);
        
        int max_np = cols - 35;  // Leave room for download status
        char npbuf[512];
        strncpy(npbuf, title, sizeof(npbuf) - 1);
        npbuf[sizeof(npbuf) - 1] = '\0';
        if ((int)strlen(npbuf) > max_np && max_np > 3) {
            npbuf[max_np - 3] = '.';
            npbuf[max_np - 2] = '.';
            npbuf[max_np - 1] = '.';
            npbuf[max_np] = '\0';
        }
        printw("%s", npbuf);
        attroff(A_BOLD);
        
        if (st->paused) {
            printw(" [PAUSED]");
        }
        if (st->shuffle_mode) {
            printw(" [SHUFFLE]");
        }
        RepeatMode effective_repeat = st->repeat_mode;
        if (!st->playing_from_playlist && effective_repeat == REPEAT_ALL) {
            effective_repeat = REPEAT_OFF;
        }
        if (effective_repeat != REPEAT_OFF) {
            printw(" [REPEAT:%s]", repeat_mode_label(effective_repeat));
        }
    }
    
    // NEW: Draw surikata status indicator
    if (st->surikata_online) {
        const char *indicator = "SYNC->OK";
        int ind_x = cols - (int)strlen(indicator) - 1;
        // Shift left if download status will be shown too
        attron(A_DIM);
        if (ind_x > 0) {
            mvprintw(rows - 1, ind_x, "%s", indicator);
        }
        attroff(A_DIM);
    }

    // NEW: Draw download status
    draw_download_status(st, rows, cols);
}

static void draw_search_view(AppState *st, const char *status, int rows, int cols) {
    mvprintw(4, 0, "Query: ");
    attron(A_BOLD);
    printw("%s", st->query[0] ? st->query : "(none)");
    attroff(A_BOLD);

    mvprintw(4, cols - 20, "Results: %d", st->search_count);

    if (status && status[0]) {
        mvprintw(5, 0, ">>> %s", status);
    }

    mvhline(6, 0, ACS_HLINE, cols);

    int list_top = 7;
    int list_height = rows - list_top - 2;
    if (list_height < 1) list_height = 1;

    // Adjust scroll
    if (st->search_selected < st->search_scroll) {
        st->search_scroll = st->search_selected;
    } else if (st->search_selected >= st->search_scroll + list_height) {
        st->search_scroll = st->search_selected - list_height + 1;
    }

    for (int i = 0; i < list_height && (st->search_scroll + i) < st->search_count; i++) {
        int idx = st->search_scroll + i;
        bool is_selected = (idx == st->search_selected);
        bool is_playing = (!st->playing_from_playlist && idx == st->playing_index);

        int y = list_top + i;
        move(y, 0);
        clrtoeol();

        char mark = ' ';
        if (is_playing) {
            mark = st->paused ? '|' : '>';
            attron(A_BOLD);
        }
        if (is_selected) {
            attron(A_REVERSE);
        }

        char dur[16];
        format_duration(st->search_results[idx].duration, dur);

        // Check if song is downloaded
        char local_path[2048];
        bool is_downloaded = get_local_file_path_for_song(st, NULL,
                                                           st->search_results[idx].video_id,
                                                           local_path, sizeof(local_path));
        const char *dl_mark = is_downloaded ? "[D]" : "   ";

        int max_title = cols - 20;
        if (max_title < 20) max_title = 20;

        char titlebuf[1024];
        const char *title = st->search_results[idx].title ? st->search_results[idx].title : "(no title)";
        strncpy(titlebuf, title, sizeof(titlebuf) - 1);
        titlebuf[sizeof(titlebuf) - 1] = '\0';

        if ((int)strlen(titlebuf) > max_title && max_title > 3) {
            titlebuf[max_title - 3] = '.';
            titlebuf[max_title - 2] = '.';
            titlebuf[max_title - 1] = '.';
            titlebuf[max_title] = '\0';
        }

        mvprintw(y, 0, " %c %3d. %s [%s] %s", mark, idx + 1, dl_mark, dur, titlebuf);
        
        if (is_selected) {
            attroff(A_REVERSE);
        }
        if (is_playing) {
            attroff(A_BOLD);
        }
    }
}

static void draw_playlists_view(AppState *st, const char *status, int rows, int cols) {
    mvprintw(4, 0, "Playlists");
    mvprintw(4, cols - 20, "Total: %d", st->playlist_count);

    if (status && status[0]) {
        mvprintw(5, 0, ">>> %s", status);
    }

    mvhline(6, 0, ACS_HLINE, cols);

    int list_top = 7;
    int list_height = rows - list_top - 2;
    if (list_height < 1) list_height = 1;
    
    if (st->playlist_count == 0) {
        mvprintw(list_top + 1, 2, "No playlists yet. Press 'c' to create one.");
        return;
    }
    
    // Adjust scroll
    if (st->playlist_selected < st->playlist_scroll) {
        st->playlist_scroll = st->playlist_selected;
    } else if (st->playlist_selected >= st->playlist_scroll + list_height) {
        st->playlist_scroll = st->playlist_selected - list_height + 1;
    }
    
    for (int i = 0; i < list_height && (st->playlist_scroll + i) < st->playlist_count; i++) {
        int idx = st->playlist_scroll + i;
        bool is_selected = (idx == st->playlist_selected);
        
        int y = list_top + i;
        move(y, 0);
        clrtoeol();
        
        if (is_selected) {
            attron(A_REVERSE);
        }
        
        // Load song count if needed
        Playlist *pl = &st->playlists[idx];
        if (pl->count == 0) {
            load_playlist_songs(st, idx);
        }

        mvprintw(y, 0, "   %3d. %s (%d songs)", idx + 1, pl->name, pl->count);

        if (pl->is_shared) {
            if (is_selected) attroff(A_REVERSE);
            attron(A_DIM);
            printw(" [shared]");
            attroff(A_DIM);
            if (is_selected) attron(A_REVERSE);
        }

        if (is_selected) {
            attroff(A_REVERSE);
        }
    }
}

static void draw_playlist_songs_view(AppState *st, const char *status, int rows, int cols) {
    if (st->current_playlist_idx < 0 || st->current_playlist_idx >= st->playlist_count) {
        return;
    }
    
    Playlist *pl = &st->playlists[st->current_playlist_idx];

    mvprintw(4, 0, "Playlist: ");
    attron(A_BOLD);
    printw("%s", pl->name);
    if (pl->is_youtube_playlist) printw(" [YT]");
    attroff(A_BOLD);
    if (pl->is_shared) {
        attron(A_DIM);
        printw(" [shared]");
        attroff(A_DIM);
    }

    mvprintw(4, cols - 20, "Songs: %d", pl->count);

    if (status && status[0]) {
        mvprintw(5, 0, ">>> %s", status);
    }

    mvhline(6, 0, ACS_HLINE, cols);

    int list_top = 7;
    int list_height = rows - list_top - 2;
    if (list_height < 1) list_height = 1;
    
    if (pl->count == 0) {
        mvprintw(list_top + 1, 2, "Playlist is empty. Search for songs and press 'a' to add.");
        return;
    }
    
    // Adjust scroll
    if (st->playlist_song_selected < st->playlist_song_scroll) {
        st->playlist_song_scroll = st->playlist_song_selected;
    } else if (st->playlist_song_selected >= st->playlist_song_scroll + list_height) {
        st->playlist_song_scroll = st->playlist_song_selected - list_height + 1;
    }
    
    for (int i = 0; i < list_height && (st->playlist_song_scroll + i) < pl->count; i++) {
        int idx = st->playlist_song_scroll + i;
        bool is_selected = (idx == st->playlist_song_selected);
        bool is_playing = (st->playing_from_playlist && 
                          st->playing_playlist_idx == st->current_playlist_idx &&
                          st->playing_index == idx);
        
        int y = list_top + i;
        move(y, 0);
        clrtoeol();
        
        char mark = ' ';
        if (is_playing) {
            mark = st->paused ? '|' : '>';
            attron(A_BOLD);
        }
        if (is_selected) {
            attron(A_REVERSE);
        }
        
        char dur[16];
        format_duration(pl->items[idx].duration, dur);

        // Check if song is downloaded
        char local_path[2048];
        bool is_downloaded = get_local_file_path_for_song(st, pl->name,
                                                           pl->items[idx].video_id,
                                                           local_path, sizeof(local_path));
        const char *dl_mark = is_downloaded ? "[D]" : "   ";

        int max_title = cols - 20;
        if (max_title < 20) max_title = 20;

        char titlebuf[1024];
        const char *title = pl->items[idx].title ? pl->items[idx].title : "(no title)";
        strncpy(titlebuf, title, sizeof(titlebuf) - 1);
        titlebuf[sizeof(titlebuf) - 1] = '\0';

        if ((int)strlen(titlebuf) > max_title && max_title > 3) {
            titlebuf[max_title - 3] = '.';
            titlebuf[max_title - 2] = '.';
            titlebuf[max_title - 1] = '.';
            titlebuf[max_title] = '\0';
        }

        mvprintw(y, 0, " %c %3d. %s [%s] %s", mark, idx + 1, dl_mark, dur, titlebuf);
        
        if (is_selected) {
            attroff(A_REVERSE);
        }
        if (is_playing) {
            attroff(A_BOLD);
        }
    }
}

static void draw_add_to_playlist_view(AppState *st, const char *status, int rows, int cols) {
    mvprintw(2, 0, "Add to playlist: ");
    if (st->song_to_add && st->song_to_add->title) {
        attron(A_BOLD);
        int max_title = cols - 20;
        char titlebuf[256];
        strncpy(titlebuf, st->song_to_add->title, sizeof(titlebuf) - 1);
        titlebuf[sizeof(titlebuf) - 1] = '\0';
        if ((int)strlen(titlebuf) > max_title && max_title > 3) {
            titlebuf[max_title - 3] = '.';
            titlebuf[max_title - 2] = '.';
            titlebuf[max_title - 1] = '.';
            titlebuf[max_title] = '\0';
        }
        printw("%s", titlebuf);
        attroff(A_BOLD);
    }
    
    if (status && status[0]) {
        mvprintw(5, 0, ">>> %s", status);
    }

    mvhline(6, 0, ACS_HLINE, cols);

    int list_top = 7;
    int list_height = rows - list_top - 2;
    if (list_height < 1) list_height = 1;

    if (st->playlist_count == 0) {
        mvprintw(list_top + 1, 2, "No playlists yet. Press 'c' to create one.");
        return;
    }
    
    // Adjust scroll
    if (st->add_to_playlist_selected < st->add_to_playlist_scroll) {
        st->add_to_playlist_scroll = st->add_to_playlist_selected;
    } else if (st->add_to_playlist_selected >= st->add_to_playlist_scroll + list_height) {
        st->add_to_playlist_scroll = st->add_to_playlist_selected - list_height + 1;
    }
    
    for (int i = 0; i < list_height && (st->add_to_playlist_scroll + i) < st->playlist_count; i++) {
        int idx = st->add_to_playlist_scroll + i;
        bool is_selected = (idx == st->add_to_playlist_selected);
        
        int y = list_top + i;
        move(y, 0);
        clrtoeol();
        
        if (is_selected) {
            attron(A_REVERSE);
        }
        
        Playlist *pl = &st->playlists[idx];
        mvprintw(y, 0, "   %3d. %s (%d songs)", idx + 1, pl->name, pl->count);
        
        if (is_selected) {
            attroff(A_REVERSE);
        }
    }
}

// NEW: Draw settings view
static void draw_settings_view(AppState *st, const char *status, int rows, int cols) {
    (void)rows; // Suppress unused parameter warning
    mvprintw(4, 0, "Settings");

    if (status && status[0]) {
        mvprintw(5, 0, ">>> %s", status);
    }

    mvhline(6, 0, ACS_HLINE, cols);

    int y = 8;

    // Setting 0: Download Path
    bool is_selected = (st->settings_selected == 0);

    mvprintw(y, 2, "Download Path:");
    y++;

    if (is_selected) {
        attron(A_REVERSE);
    }

    if (st->settings_editing && is_selected) {
        // Show edit buffer with cursor
        mvprintw(y, 4, "%-*s", cols - 8, st->settings_edit_buffer);

        // Position cursor
        move(y, 4 + st->settings_edit_pos);
        curs_set(1);
    } else {
        // Show current value
        int max_path = cols - 8;
        char pathbuf[1024];
        strncpy(pathbuf, st->config.download_path, sizeof(pathbuf) - 1);
        pathbuf[sizeof(pathbuf) - 1] = '\0';

        if ((int)strlen(pathbuf) > max_path && max_path > 3) {
            // Truncate from the beginning to show the end of the path
            int offset = strlen(pathbuf) - max_path + 3;
            memmove(pathbuf + 3, pathbuf + offset, strlen(pathbuf) - offset + 1);
            pathbuf[0] = '.';
            pathbuf[1] = '.';
            pathbuf[2] = '.';
        }

        mvprintw(y, 4, "%s", pathbuf);
        curs_set(0);
    }

    if (is_selected) {
        attroff(A_REVERSE);
    }

    y += 2;

    // Setting 1: Seek Step
    is_selected = (st->settings_selected == 1);
    if (is_selected) attron(A_REVERSE);
    mvprintw(y, 2, "Seek Step (seconds): %d", st->config.seek_step);
    if (is_selected) attroff(A_REVERSE);
    y += 2;

    // Setting 2: Remember Session
    is_selected = (st->settings_selected == 2);
    if (is_selected) attron(A_REVERSE);
    mvprintw(y, 2, "Remember Session: %s", st->config.remember_session ? "ON" : "OFF");
    if (is_selected) attroff(A_REVERSE);
    y += 2;

    // Setting 3: Shuffle Mode
    is_selected = (st->settings_selected == 3);
    if (is_selected) attron(A_REVERSE);
    mvprintw(y, 2, "Shuffle Mode: %s", st->shuffle_mode ? "ON" : "OFF");
    if (is_selected) attroff(A_REVERSE);
    y += 2;

    // Setting 4: Repeat Mode
    is_selected = (st->settings_selected == 4);
    if (is_selected) attron(A_REVERSE);
    mvprintw(y, 2, "Repeat Mode: %s", repeat_mode_label(st->repeat_mode));
    if (is_selected) attroff(A_REVERSE);
    y += 2;

    // Setting 5: Search Results
    is_selected = (st->settings_selected == 5);
    if (is_selected) attron(A_REVERSE);
    mvprintw(y, 2, "Search Results: %d", st->config.max_results);
    if (is_selected) attroff(A_REVERSE);
    y += 2;

    // Section header for cookies
    mvhline(y, 2, ACS_HLINE, cols - 4);
    mvprintw(y, 4, " YouTube Cookies (workaround for YT bot check) ");
    y += 2;

    // Setting 6: Cookies Mode
    is_selected = (st->settings_selected == 6);
    if (is_selected) attron(A_REVERSE);
    const char *mode_label = "Off";
    if (st->config.yt_cookies_mode == YT_COOKIES_AUTO) mode_label = "Auto from browser";
    else if (st->config.yt_cookies_mode == YT_COOKIES_MANUAL) mode_label = "Manual file";
    mvprintw(y, 2, "Cookies Mode: %s", mode_label);
    if (is_selected) attroff(A_REVERSE);
    y += 2;

    // Setting 7: Browser (cycles through supported browsers)
    is_selected = (st->settings_selected == 7);
    bool browser_active = (st->config.yt_cookies_mode == YT_COOKIES_AUTO);
    if (is_selected) attron(A_REVERSE);
    if (browser_active) {
        mvprintw(y, 2, "Browser: %s",
                 st->config.yt_cookies_browser[0] ? st->config.yt_cookies_browser : "(not set)");
    } else {
        mvprintw(y, 2, "Browser: %s (set Mode to Auto)",
                 st->config.yt_cookies_browser[0] ? st->config.yt_cookies_browser : "—");
    }
    if (is_selected) attroff(A_REVERSE);
    y += 2;

    // Setting 8: Cookies File path (text input when editing)
    is_selected = (st->settings_selected == 8);
    bool file_active = (st->config.yt_cookies_mode == YT_COOKIES_MANUAL);
    if (is_selected) attron(A_REVERSE);
    if (st->settings_editing && is_selected) {
        mvprintw(y, 2, "Cookies File:");
        mvprintw(y + 1, 4, "%-*s", cols - 8, st->settings_edit_buffer);
        move(y + 1, 4 + st->settings_edit_pos);
        curs_set(1);
    } else {
        const char *fpath = st->config.yt_cookies_file[0] ? st->config.yt_cookies_file : "(not set)";
        if (file_active) {
            mvprintw(y, 2, "Cookies File: %s", fpath);
        } else {
            mvprintw(y, 2, "Cookies File: %s (set Mode to Manual)", fpath);
        }
        curs_set(0);
    }
    if (is_selected) attroff(A_REVERSE);
    y += st->settings_editing && is_selected ? 3 : 2;

    // Setting 9: Test cookies (action)
    is_selected = (st->settings_selected == 9);
    if (is_selected) attron(A_REVERSE);
    mvprintw(y, 2, "[ Test cookies (run a real fetch) ]");
    if (is_selected) attroff(A_REVERSE);
    y += 2;

    // Setting 10: Help link with clipboard copy
    is_selected = (st->settings_selected == 10);
    if (is_selected) attron(A_REVERSE);
    mvprintw(y, 2, "[ Copy guide link: https://surikata.app/g/9fa4af84829f ]");
    if (is_selected) attroff(A_REVERSE);
    y += 2;

    // Help text
    mvprintw(y, 2, "Up/Down: navigate | Enter: edit/toggle/run | Esc: back");
    y++;

    if (st->settings_editing) {
        mvprintw(y, 2, "Editing: Enter to save, Esc to cancel");
    }
}

// NEW: Draw exit confirmation dialog when downloads are pending
static void draw_exit_dialog(AppState *st, int pending_count) {
    (void)st; // Suppress unused parameter warning
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    
    int dialog_w = 50;
    int dialog_h = 8;
    int start_x = (cols - dialog_w) / 2;
    int start_y = (rows - dialog_h) / 2;
    
    // Draw box background
    for (int y = start_y; y < start_y + dialog_h; y++) {
        mvhline(y, start_x, ' ', dialog_w);
    }
    
    // Draw title bar
    attron(A_REVERSE);
    mvprintw(start_y, start_x, "%-*s", dialog_w, "");
    mvprintw(start_y, start_x + (dialog_w - 16) / 2, " Download Queue ");
    attroff(A_REVERSE);
    
    // Draw content
    mvprintw(start_y + 2, start_x + 2, "Downloads in progress: %d remaining", pending_count);
    mvprintw(start_y + 4, start_x + 2, "Downloads will resume on next startup.");
    
    // Draw options
    attron(A_BOLD);
    mvprintw(start_y + 6, start_x + 2, "[q] Quit anyway    [Esc] Cancel");
    attroff(A_BOLD);
    
    refresh();
}

// NEW: Draw About overlay
static void draw_about_view(AppState *st, const char *status, int rows, int cols) {
    (void)st; // Unused
    (void)status; // Unused

    int dialog_w = 60;
    int dialog_h = 16;
    int start_x = (cols - dialog_w) / 2;
    int start_y = (rows - dialog_h) / 2;

    // Draw box background
    attron(A_BOLD);
    for (int y = start_y; y < start_y + dialog_h; y++) {
        mvhline(y, start_x, ' ', dialog_w);
    }
    attroff(A_BOLD);

    // Draw border
    attron(A_BOLD);
    mvaddch(start_y, start_x, ACS_ULCORNER);
    mvaddch(start_y, start_x + dialog_w - 1, ACS_URCORNER);
    mvaddch(start_y + dialog_h - 1, start_x, ACS_LLCORNER);
    mvaddch(start_y + dialog_h - 1, start_x + dialog_w - 1, ACS_LRCORNER);
    mvhline(start_y, start_x + 1, ACS_HLINE, dialog_w - 2);
    mvhline(start_y + dialog_h - 1, start_x + 1, ACS_HLINE, dialog_w - 2);
    mvvline(start_y + 1, start_x, ACS_VLINE, dialog_h - 2);
    mvvline(start_y + 1, start_x + dialog_w - 1, ACS_VLINE, dialog_h - 2);
    attroff(A_BOLD);

    // Title
    attron(A_BOLD | A_REVERSE);
    mvprintw(start_y + 2, start_x + (dialog_w - 15) / 2, " ShellBeats v%s", SHELLBEATS_VERSION);
    attroff(A_BOLD | A_REVERSE);

    // Version and description
    mvprintw(start_y + 4, start_x + (dialog_w - 28) / 2, "made by Lalo for Nami & Elia");
    mvprintw(start_y + 6, start_x + (dialog_w - 44) / 2, "A terminal-based music player for YouTube");

    // Features
    mvprintw(start_y + 8, start_x + 4, "Features:");
    mvprintw(start_y + 9, start_x + 6, "* Search and stream music from YouTube");
    mvprintw(start_y + 10, start_x + 6, "* Download songs as MP3");
    mvprintw(start_y + 11, start_x + 6, "* Create and manage playlists");
    mvprintw(start_y + 12, start_x + 6, "* Offline playback from local files");

    // Footer
    attron(A_DIM);
    mvprintw(start_y + 14, start_x + (dialog_w - 40) / 2, "Built with mpv, yt-dlp, and ncurses");
    attroff(A_DIM);

    refresh();
}

// NEW: Draw SuriSync overlay menu
static void draw_surisync_view(AppState *st, const char *status, int rows, int cols) {
    (void)status;

    int dialog_w = 50;
    int dialog_h = 20;
    int start_x = (cols - dialog_w) / 2;
    int start_y = (rows - dialog_h) / 2;

    // Draw box background
    for (int y = start_y; y < start_y + dialog_h; y++) {
        mvhline(y, start_x, ' ', dialog_w);
    }

    // Draw border
    attron(A_BOLD);
    mvaddch(start_y, start_x, ACS_ULCORNER);
    mvaddch(start_y, start_x + dialog_w - 1, ACS_URCORNER);
    mvaddch(start_y + dialog_h - 1, start_x, ACS_LLCORNER);
    mvaddch(start_y + dialog_h - 1, start_x + dialog_w - 1, ACS_LRCORNER);
    mvhline(start_y, start_x + 1, ACS_HLINE, dialog_w - 2);
    mvhline(start_y + dialog_h - 1, start_x + 1, ACS_HLINE, dialog_w - 2);
    mvvline(start_y + 1, start_x, ACS_VLINE, dialog_h - 2);
    mvvline(start_y + 1, start_x + dialog_w - 1, ACS_VLINE, dialog_h - 2);
    attroff(A_BOLD);

    // Title
    const char *title = " SuriSync ";
    mvprintw(start_y, start_x + (dialog_w - (int)strlen(title)) / 2, "%s", title);

    // Load config to show current state
    sb_sync_config_t cfg = sb_load_config(st->config_dir);
    bool linked = (strlen(cfg.token) > 0);

    // Determine current playlist state for share/unshare labels
    bool has_current_pl = (st->surisync_return_view == VIEW_PLAYLIST_SONGS &&
                           st->current_playlist_idx >= 0 &&
                           st->current_playlist_idx < st->playlist_count);
    bool current_shared = has_current_pl && st->playlists[st->current_playlist_idx].is_shared;
    const char *current_pl_name = has_current_pl ? st->playlists[st->current_playlist_idx].name : NULL;

    // Menu items (labels)
    const char *items[] = {
        "Status",
        "Link to Surikata",
        "Unlink",
        "Push playlists",
        "Get playlists",
        NULL, // dynamic: share
        NULL, // dynamic: unshare
        NULL, // separator
        NULL, // auto-sync get toggle
        NULL, // auto-sync push toggle
    };
    int item_count = 10;

    // Help text per item
    const char *help_lines[][3] = {
        /* 0 Status    */ {"Check connection to surikata.app", NULL, NULL},
        /* 1 Link      */ {"Register free at surikata.app", "Setup > Apps > Generate Token, copy/paste", "Enjoy the community, more tools coming!"},
        /* 2 Unlink    */ {"Remove your token and disconnect", NULL, NULL},
        /* 3 Push      */ {"Upload all your playlists to Surikata", NULL, NULL},
        /* 4 Get       */ {"Download your playlists from Surikata", NULL, NULL},
        /* 5 Share     */ {"Make this playlist visible on your profile", NULL, NULL},
        /* 6 Unshare   */ {"Hide this playlist from your profile", NULL, NULL},
        /* 7 sep       */ {NULL, NULL, NULL},
        /* 8 Auto get  */ {"Automatically download playlists on startup", NULL, NULL},
        /* 9 Auto push */ {"Automatically upload playlists on exit", NULL, NULL},
    };

    int content_x = start_x + 3;
    int y = start_y + 2;

    for (int i = 0; i < item_count; i++) {
        if (i == 7) {
            // Separator + section label
            attron(A_DIM);
            mvhline(y, start_x + 2, ACS_HLINE, dialog_w - 4);
            attroff(A_DIM);
            y++;
            attron(A_DIM);
            mvprintw(y, content_x, "Enable both to keep your library in sync");
            attroff(A_DIM);
            y++;
            continue;
        }

        bool selected = (st->surisync_selected == i);

        if (selected) attron(A_REVERSE);

        // Clear line
        mvhline(y, start_x + 1, ' ', dialog_w - 2);

        if (i == 8) {
            mvprintw(y, content_x, "[%c] Auto-sync on startup (get)",
                     cfg.pull_on_startup ? 'x' : ' ');
        } else if (i == 9) {
            mvprintw(y, content_x, "[%c] Auto-sync on quit (push)",
                     cfg.sync_on_quit ? 'x' : ' ');
        } else if (i == 5) {
            const char *marker = selected ? ">" : " ";
            if (has_current_pl) {
                mvprintw(y, content_x - 1, "%s Share: %s", marker, current_pl_name);
                if (current_shared) {
                    if (selected) attroff(A_REVERSE);
                    attron(A_DIM);
                    printw(" [shared]");
                    attroff(A_DIM);
                    if (selected) attron(A_REVERSE);
                }
            } else {
                mvprintw(y, content_x - 1, "%s Share playlist", marker);
                attron(A_DIM);
                printw(" (open one first)");
                attroff(A_DIM);
                if (selected) attron(A_REVERSE);
            }
        } else if (i == 6) {
            const char *marker = selected ? ">" : " ";
            if (has_current_pl && current_shared) {
                mvprintw(y, content_x - 1, "%s Unshare: %s", marker, current_pl_name);
            } else if (has_current_pl) {
                mvprintw(y, content_x - 1, "%s Unshare: %s", marker, current_pl_name);
                attron(A_DIM);
                printw(" (not shared)");
                attroff(A_DIM);
                if (selected) attron(A_REVERSE);
            } else {
                mvprintw(y, content_x - 1, "%s Unshare playlist", marker);
                attron(A_DIM);
                printw(" (open one first)");
                attroff(A_DIM);
                if (selected) attron(A_REVERSE);
            }
        } else {
            const char *marker = selected ? ">" : " ";
            mvprintw(y, content_x - 1, "%s %s", marker, items[i]);

            // Show linked status next to Status item
            if (i == 0 && linked) {
                printw("  [linked]");
            } else if (i == 0) {
                printw("  [not linked]");
            }
        }

        if (selected) attroff(A_REVERSE);
        y++;
    }

    // Help area: show help for currently selected item
    int help_y = start_y + dialog_h - 5;
    attron(A_DIM);
    mvhline(help_y, start_x + 2, ACS_HLINE, dialog_w - 4);
    attroff(A_DIM);
    help_y++;

    // Clear help lines
    for (int h = 0; h < 3; h++) {
        mvhline(help_y + h, start_x + 1, ' ', dialog_w - 2);
    }

    int sel = st->surisync_selected;
    if (sel >= 0 && sel < item_count && sel != 7) {
        attron(A_DIM);
        for (int h = 0; h < 3; h++) {
            if (help_lines[sel][h]) {
                mvprintw(help_y + h, content_x, "%s", help_lines[sel][h]);
            }
        }
        attroff(A_DIM);
    }

    // Footer
    attron(A_DIM);
    mvprintw(start_y + dialog_h - 2, start_x + 2,
             "Enter: select  Esc: close");
    attroff(A_DIM);

    refresh();
}

static void draw_ui(AppState *st, const char *status) {
    erase();
    
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    
    draw_header(st, cols, st->view);
    
    switch (st->view) {
        case VIEW_SEARCH:
            draw_search_view(st, status, rows, cols);
            break;
        case VIEW_PLAYLISTS:
            draw_playlists_view(st, status, rows, cols);
            break;
        case VIEW_PLAYLIST_SONGS:
            draw_playlist_songs_view(st, status, rows, cols);
            break;
        case VIEW_ADD_TO_PLAYLIST:
            draw_add_to_playlist_view(st, status, rows, cols);
            break;
        case VIEW_SETTINGS:
            draw_settings_view(st, status, rows, cols);
            break;
        case VIEW_ABOUT:
            draw_about_view(st, status, rows, cols);
            break;
        case VIEW_SURISYNC:
            draw_surisync_view(st, status, rows, cols);
            break;
    }
    
    draw_now_playing(st, rows, cols);
    
    refresh();
}

// ============================================================================
// YouTube Playlist Progress Callback
// ============================================================================

static void youtube_fetch_progress_callback(int count, const char *message, void *user_data) {
    (void)count; // Suppress unused parameter warning
    char *status_buf = (char *)user_data;
    if (status_buf && message) {
        strncpy(status_buf, message, 511);
        status_buf[511] = '\0';
        
        // Redraw UI to show progress
        if (g_app_state) {
            draw_ui(g_app_state, status_buf);
            refresh(); // Force screen update
        }
    }
}

// ============================================================================
// Input Handling
// ============================================================================

static int get_string_input(char *buf, size_t bufsz, const char *prompt) {
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    
    int y = rows - 1;
    move(y, 0);
    clrtoeol();
    
    attron(A_BOLD);
    mvprintw(y, 0, "%s", prompt);
    attroff(A_BOLD);
    refresh();
    
    int prompt_len = strlen(prompt);
    int max_input = cols - prompt_len - 2;
    if (max_input > (int)bufsz - 1) max_input = bufsz - 1;
    if (max_input < 1) max_input = 1;
    
    // Disable timeout for blocking input
    timeout(-1);
    
    echo();
    curs_set(1);
    move(y, prompt_len);
    
    getnstr(buf, max_input);
    
    noecho();
    curs_set(0);
    
    // Re-enable timeout for poll-based event checking
    timeout(100);
    
    char *trimmed = trim_whitespace(buf);
    if (trimmed != buf) {
        memmove(buf, trimmed, strlen(trimmed) + 1);
    }
    
    return strlen(buf);
}

static void show_help(void) {
    erase();
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    (void)cols;

    int y = 2;
    attron(A_BOLD);
    mvprintw(y++, 2, "ShellBeats v%s | Help", SHELLBEATS_VERSION);
    attroff(A_BOLD);
    y++;

    mvprintw(y++, 4, "PLAYBACK:");
    mvprintw(y++, 6, "/,s         Search YouTube");
    mvprintw(y++, 6, "Enter       Play selected");
    mvprintw(y++, 6, "Space       Pause/Resume");
    mvprintw(y++, 6, "n/p         Next/Previous track");
    mvprintw(y++, 6, "x           Stop playback");
    mvprintw(y++, 6, "R           Toggle shuffle mode");
    mvprintw(y++, 6, "L           Cycle repeat (OFF/ALL/ONE)");
    mvprintw(y++, 6, "Left/Right  Seek backward/forward");
    mvprintw(y++, 6, "t           Jump to time (mm:ss)");
    y++;

    mvprintw(y++, 4, "NAVIGATION:");
    mvprintw(y++, 6, "Up/Down/j/k Navigate list");
    mvprintw(y++, 6, "PgUp/PgDn   Page up/down");
    mvprintw(y++, 6, "g/G         Go to start/end");
    mvprintw(y++, 6, "Esc         Go back");
    y++;

    mvprintw(y++, 4, "PLAYLISTS:");
    mvprintw(y++, 6, "f           Open playlists menu");
    mvprintw(y++, 6, "a           Add song to playlist");
    mvprintw(y++, 6, "c           Create new playlist");
    mvprintw(y++, 6, "e           Rename playlist");
    mvprintw(y++, 6, "X           Remove song from playlist");
    mvprintw(y++, 6, "d/D         Download song / Download all");
    mvprintw(y++, 6, "p           Import YouTube playlist");
    mvprintw(y++, 6, "u           Sync YouTube playlist");
    mvprintw(y++, 6, "x           Delete playlist");
    y++;

    mvprintw(y++, 4, "OTHER:");
    mvprintw(y++, 6, "S           Settings");
    mvprintw(y++, 6, "i           About");
    mvprintw(y++, 6, "h,?         Show this help");
    mvprintw(y++, 6, "q           Quit");

    attron(A_REVERSE);
    mvprintw(rows - 2, 2, " Press any key to continue... ");
    attroff(A_REVERSE);

    refresh();
    timeout(-1);
    getch();
    timeout(100);
}

static bool check_dependencies(AppState *st, char *errmsg, size_t errsz) {
    // yt-dlp: accept local binary OR system binary
    bool ytdlp_found = false;
    if (st->ytdlp_has_local && file_exists(st->ytdlp_local_path)) {
        ytdlp_found = true;
    } else {
        FILE *fp = popen("which yt-dlp 2>/dev/null", "r");
        if (fp) {
            char buf[256];
            ytdlp_found = (fgets(buf, sizeof(buf), fp) != NULL && buf[0] == '/');
            pclose(fp);
        }
    }
    if (!ytdlp_found && !st->ytdlp_updating) {
        snprintf(errmsg, errsz, "yt-dlp not found! Will be downloaded automatically on next start.");
        return false;
    }
    
    FILE *mpv_fp = popen("which mpv 2>/dev/null", "r");
    if (mpv_fp) {
        char buf[256];
        bool found = (fgets(buf, sizeof(buf), mpv_fp) != NULL && buf[0] == '/');
        pclose(mpv_fp);
        if (!found) {
            snprintf(errmsg, errsz, "mpv not found! Install with: apt install mpv");
            return false;
        }
    }
    
    return true;
}

// ============================================================================
// Surikata Sync Helpers
// ============================================================================

// Convert internal Playlist to sb_playlist_t for sync
static sb_playlist_t playlist_to_sb(AppState *st, int idx) {
    sb_playlist_t sb = {0};
    if (idx < 0 || idx >= st->playlist_count) return sb;

    Playlist *pl = &st->playlists[idx];
    snprintf(sb.name, sizeof(sb.name), "%s", pl->name);
    snprintf(sb.type, sizeof(sb.type), "%s", pl->is_youtube_playlist ? "youtube" : "local");
    sb.is_shared = pl->is_shared;
    sb.song_count = pl->count;

    if (pl->count > 0) {
        sb.songs = calloc(pl->count, sizeof(sb_song_t));
        if (sb.songs) {
            for (int i = 0; i < pl->count; i++) {
                if (pl->items[i].title)
                    snprintf(sb.songs[i].title, sizeof(sb.songs[i].title), "%s", pl->items[i].title);
                if (pl->items[i].video_id)
                    snprintf(sb.songs[i].video_id, sizeof(sb.songs[i].video_id), "%s", pl->items[i].video_id);
                sb.songs[i].duration = pl->items[i].duration;
            }
        }
    }

    return sb;
}

// Auto-sync a single playlist to server (non-blocking via double-fork)
static void auto_sync_playlist(AppState *st, int idx) {
    sb_sync_config_t cfg = sb_load_config(st->config_dir);
    if (!cfg.enabled || strlen(cfg.token) == 0) return;

    // Load playlist songs if not loaded
    if (st->playlists[idx].count == 0) {
        load_playlist_songs(st, idx);
    }

    sb_playlist_t sb = playlist_to_sb(st, idx);

    // Double-fork to avoid zombie: parent waits for child, child forks grandchild and exits
    pid_t pid = fork();
    if (pid == 0) {
        pid_t pid2 = fork();
        if (pid2 == 0) {
            // Grandchild: does the actual work, adopted by init
            sb_sync_init();
            sb_push_playlist(&cfg, &sb);
            sb_sync_cleanup();
            sb_free_playlist(&sb);
            _exit(0);
        }
        _exit(0); // Child exits immediately
    } else if (pid > 0) {
        waitpid(pid, NULL, 0); // Reap child instantly
    }
    sb_free_playlist(&sb);
}

// Write playlists to local JSON files from pull result
static void write_pulled_playlists(AppState *st, const sb_pull_result_t *pull) {
    for (int i = 0; i < pull->playlist_count; i++) {
        sb_playlist_t *sb = &pull->playlists[i];

        // Check if playlist already exists
        int existing_idx = -1;
        for (int j = 0; j < st->playlist_count; j++) {
            if (strcasecmp(st->playlists[j].name, sb->name) == 0) {
                existing_idx = j;
                break;
            }
        }

        int pl_idx;
        if (existing_idx >= 0) {
            pl_idx = existing_idx;
            // Clear existing songs
            free_playlist_items(&st->playlists[pl_idx]);
        } else {
            // Create new playlist
            bool is_yt = strcmp(sb->type, "youtube") == 0;
            pl_idx = create_playlist(st, sb->name, is_yt);
            if (pl_idx < 0) continue;
        }

        // Set shared state from server
        st->playlists[pl_idx].is_shared = sb->is_shared;

        // Add songs
        Playlist *pl = &st->playlists[pl_idx];
        for (int s = 0; s < sb->song_count && pl->count < MAX_PLAYLIST_ITEMS; s++) {
            int idx = pl->count;
            pl->items[idx].title = strdup(sb->songs[s].title);
            pl->items[idx].video_id = strdup(sb->songs[s].video_id);
            char url[256];
            snprintf(url, sizeof(url), "https://www.youtube.com/watch?v=%s", sb->songs[s].video_id);
            pl->items[idx].url = strdup(url);
            pl->items[idx].duration = sb->songs[s].duration;
            pl->count++;
        }

        save_playlist(st, pl_idx);
    }
    save_playlists_index(st);
}

// Merge followed playlists (additive-only: add new songs, never remove)
// Returns total number of new songs added across all playlists
static int merge_followed_playlists(AppState *st, const sb_follow_check_result_t *follows) {
    int total_added = 0;

    for (int i = 0; i < follows->count; i++) {
        sb_followed_playlist_t *fp = &follows->playlists[i];

        // Build local playlist name: "owner/name"
        char local_name[256];
        snprintf(local_name, sizeof(local_name), "%s/%s", fp->owner, fp->name);

        // Find or create local playlist
        int pl_idx = -1;
        for (int j = 0; j < st->playlist_count; j++) {
            if (strcasecmp(st->playlists[j].name, local_name) == 0) {
                pl_idx = j;
                break;
            }
        }

        if (pl_idx < 0) {
            bool is_yt = strcmp(fp->type, "youtube") == 0;
            pl_idx = create_playlist(st, local_name, is_yt);
            if (pl_idx < 0) continue;
        }

        // Load existing songs so duplicate check works
        load_playlist_songs(st, pl_idx);

        // Add only new songs (additive-only, checks video_id duplicates)
        int added = 0;
        for (int s = 0; s < fp->song_count; s++) {
            Song song = {0};
            song.title = strdup(fp->songs[s].title);
            song.video_id = strdup(fp->songs[s].video_id);
            char url[256];
            snprintf(url, sizeof(url), "https://www.youtube.com/watch?v=%s", fp->songs[s].video_id);
            song.url = strdup(url);
            song.duration = fp->songs[s].duration;

            if (add_song_to_playlist(st, pl_idx, &song)) {
                added++;
            } else {
                // Not added (duplicate or full), free our copies
                free(song.title);
                free(song.video_id);
                free(song.url);
            }
        }

        if (added > 0) {
            save_playlist(st, pl_idx);
            total_added += added;
        }
    }

    if (total_added > 0) {
        save_playlists_index(st);
    }

    return total_added;
}

// Handle CLI subcommands (called before ncurses init)
// Returns true if a subcommand was handled (app should exit after)
static bool handle_sync_commands(int argc, char *argv[], AppState *st) {
    if (argc < 2) return false;

    const char *cmd = argv[1];

    // ── shellbeats link <token> ──
    if (strcmp(cmd, "link") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Usage: shellbeats link <token>\n");
            fprintf(stderr, "Get your token from https://surikata.app > Settings > ShellBeats\n");
            return true;
        }

        const char *token = argv[2];

        // Validate token format
        if (strncmp(token, "sb_", 3) != 0 || strlen(token) != 43) {
            fprintf(stderr, "Error: Invalid token format. Token should start with 'sb_' and be 43 characters.\n");
            return true;
        }

        sb_sync_config_t cfg = {0};
        snprintf(cfg.url, sizeof(cfg.url), "https://surikata.app");
        snprintf(cfg.token, sizeof(cfg.token), "%s", token);
        cfg.enabled = true;

        printf("Verifying token... ");
        fflush(stdout);

        sb_sync_init();
        sb_verify_result_t verify = sb_verify(&cfg);

        if (!verify.success) {
            printf("FAILED\n");
            fprintf(stderr, "Error: %s\n", verify.error_msg);
            sb_sync_cleanup();
            return true;
        }

        printf("OK\n");
        printf("Linked to Surikata as: %s\n", verify.username);

        // Save config
        sb_error_t err = sb_save_config(st->config_dir, &cfg);
        if (err != SB_OK) {
            fprintf(stderr, "Error saving config: %s\n", sb_error_str(err));
            sb_sync_cleanup();
            return true;
        }

        // Ask if user wants to pull
        if (verify.playlists_synced > 0) {
            printf("\n%d playlist(s) found on server. Download them? [y/N] ", verify.playlists_synced);
            fflush(stdout);
            int c = getchar();
            if (c == 'y' || c == 'Y') {
                printf("Pulling playlists... ");
                fflush(stdout);
                sb_pull_result_t pull = sb_pull_all(&cfg);
                if (pull.success) {
                    write_pulled_playlists(st, &pull);
                    printf("OK (%d playlists)\n", pull.playlist_count);
                    sb_free_pull_result(&pull);
                } else {
                    printf("FAILED: %s\n", pull.error_msg);
                }
            }
        }

        printf("\nSync enabled. Playlists will auto-sync on changes.\n");
        sb_sync_cleanup();
        return true;
    }

    // ── shellbeats unlink ──
    if (strcmp(cmd, "unlink") == 0) {
        sb_sync_config_t cfg = sb_load_config(st->config_dir);
        if (strlen(cfg.token) == 0) {
            printf("Not linked to Surikata.\n");
            return true;
        }

        cfg.token[0] = '\0';
        cfg.enabled = false;
        sb_save_config(st->config_dir, &cfg);
        printf("Unlinked from Surikata. Synced data on server is preserved.\n");
        return true;
    }

    // ── shellbeats sync ──
    if (strcmp(cmd, "sync") == 0) {
        sb_sync_config_t cfg = sb_load_config(st->config_dir);
        if (strlen(cfg.token) == 0) {
            fprintf(stderr, "Not linked. Run: shellbeats link <token>\n");
            return true;
        }

        printf("Syncing all playlists... ");
        fflush(stdout);

        // Build sb_playlist_t array from all playlists
        sb_playlist_t *sb_pls = calloc(st->playlist_count, sizeof(sb_playlist_t));
        if (!sb_pls && st->playlist_count > 0) {
            fprintf(stderr, "Memory error\n");
            return true;
        }

        for (int i = 0; i < st->playlist_count; i++) {
            // Ensure songs are loaded
            if (st->playlists[i].count == 0) {
                load_playlist_songs(st, i);
            }
            sb_pls[i] = playlist_to_sb(st, i);
        }

        // Read config.json for backup
        char config_path[16384];
        snprintf(config_path, sizeof(config_path), "%s/%s", st->config_dir, CONFIG_FILE);
        char *config_content = NULL;
        FILE *cf = fopen(config_path, "r");
        if (cf) {
            fseek(cf, 0, SEEK_END);
            long len = ftell(cf);
            fseek(cf, 0, SEEK_SET);
            if (len > 0 && len < 65536) {
                config_content = malloc(len + 1);
                if (config_content) {
                    size_t r = fread(config_content, 1, len, cf);
                    config_content[r] = '\0';
                }
            }
            fclose(cf);
        }

        sb_sync_init();
        sb_push_result_t result = sb_push_all(&cfg, sb_pls, st->playlist_count, config_content);

        if (result.success) {
            printf("OK (%d playlists synced)\n", result.synced_count);
        } else {
            printf("FAILED: %s\n", result.error_msg);
        }

        // Cleanup
        for (int i = 0; i < st->playlist_count; i++) {
            sb_free_playlist(&sb_pls[i]);
        }
        free(sb_pls);
        free(config_content);
        sb_sync_cleanup();
        return true;
    }

    // ── shellbeats pull ──
    if (strcmp(cmd, "pull") == 0) {
        sb_sync_config_t cfg = sb_load_config(st->config_dir);
        if (strlen(cfg.token) == 0) {
            fprintf(stderr, "Not linked. Run: shellbeats link <token>\n");
            return true;
        }

        printf("Pulling from server... ");
        fflush(stdout);

        sb_sync_init();
        sb_pull_result_t pull = sb_pull_all(&cfg);

        if (pull.success) {
            write_pulled_playlists(st, &pull);
            printf("OK (%d playlists)\n", pull.playlist_count);
            sb_free_pull_result(&pull);
        } else {
            printf("FAILED: %s\n", pull.error_msg);
        }

        sb_sync_cleanup();
        return true;
    }

    // ── shellbeats share <playlist_name> ──
    if (strcmp(cmd, "share") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Usage: shellbeats share <playlist_name>\n");
            // List available playlists
            if (st->playlist_count > 0) {
                fprintf(stderr, "\nAvailable playlists:\n");
                for (int i = 0; i < st->playlist_count; i++) {
                    fprintf(stderr, "  - %s\n", st->playlists[i].name);
                }
            }
            return true;
        }

        sb_sync_config_t cfg = sb_load_config(st->config_dir);
        if (strlen(cfg.token) == 0) {
            fprintf(stderr, "Not linked. Run: shellbeats link <token>\n");
            return true;
        }

        const char *name = argv[2];

        // Find playlist
        int found = -1;
        for (int i = 0; i < st->playlist_count; i++) {
            if (strcasecmp(st->playlists[i].name, name) == 0) {
                found = i;
                break;
            }
        }

        if (found < 0) {
            fprintf(stderr, "Playlist '%s' not found.\n", name);
            return true;
        }

        // Load songs and push as shared
        load_playlist_songs(st, found);

        // Update local state
        st->playlists[found].is_shared = true;
        save_playlist(st, found);

        sb_playlist_t sb = playlist_to_sb(st, found);

        printf("Sharing '%s'... ", name);
        fflush(stdout);

        sb_sync_init();
        sb_push_result_t result = sb_push_playlist(&cfg, &sb);
        sb_free_playlist(&sb);

        if (result.success) {
            printf("OK (visible on your Surikata profile)\n");
        } else {
            // Revert local on failure
            st->playlists[found].is_shared = false;
            save_playlist(st, found);
            printf("FAILED: %s\n", result.error_msg);
        }

        sb_sync_cleanup();
        return true;
    }

    // ── shellbeats unshare <playlist_name> ──
    if (strcmp(cmd, "unshare") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Usage: shellbeats unshare <playlist_name>\n");
            return true;
        }

        sb_sync_config_t cfg = sb_load_config(st->config_dir);
        if (strlen(cfg.token) == 0) {
            fprintf(stderr, "Not linked. Run: shellbeats link <token>\n");
            return true;
        }

        const char *name = argv[2];

        int found = -1;
        for (int i = 0; i < st->playlist_count; i++) {
            if (strcasecmp(st->playlists[i].name, name) == 0) {
                found = i;
                break;
            }
        }

        if (found < 0) {
            fprintf(stderr, "Playlist '%s' not found.\n", name);
            return true;
        }

        load_playlist_songs(st, found);

        // Update local state
        st->playlists[found].is_shared = false;
        save_playlist(st, found);

        sb_playlist_t sb = playlist_to_sb(st, found);

        printf("Unsharing '%s'... ", name);
        fflush(stdout);

        sb_sync_init();
        sb_push_result_t result = sb_push_playlist(&cfg, &sb);
        sb_free_playlist(&sb);

        if (result.success) {
            printf("OK (no longer visible on profile)\n");
        } else {
            // Revert local on failure
            st->playlists[found].is_shared = true;
            save_playlist(st, found);
            printf("FAILED: %s\n", result.error_msg);
        }

        sb_sync_cleanup();
        return true;
    }

    // ── shellbeats status ──
    if (strcmp(cmd, "status") == 0) {
        sb_sync_config_t cfg = sb_load_config(st->config_dir);
        if (strlen(cfg.token) == 0) {
            printf("Surikata sync: not linked\n");
            printf("Run: shellbeats link <token>\n");
            return true;
        }

        printf("Surikata sync: linked\n");
        printf("Server: %s\n", cfg.url);
        printf("Token: %s...%s\n",
               (char[]){cfg.token[0], cfg.token[1], cfg.token[2], cfg.token[3], '\0'},
               cfg.token + strlen(cfg.token) - 4);

        printf("Verifying... ");
        fflush(stdout);

        sb_sync_init();
        sb_verify_result_t v = sb_verify(&cfg);
        if (v.success) {
            printf("OK (user: %s, %d playlists synced)\n", v.username, v.playlists_synced);
        } else {
            printf("FAILED: %s\n", v.error_msg);
        }
        sb_sync_cleanup();

        return true;
    }

    // ── shellbeats follow <playlist_id> ──
    if (strcmp(cmd, "follow") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Usage: shellbeats follow <playlist_id>\n");
            fprintf(stderr, "Get playlist IDs from shared playlist URLs on surikata.app\n");
            return true;
        }

        int playlist_id = atoi(argv[2]);
        if (playlist_id <= 0) {
            fprintf(stderr, "Error: invalid playlist ID\n");
            return true;
        }

        sb_sync_config_t cfg = sb_load_config(st->config_dir);
        if (strlen(cfg.token) == 0) {
            fprintf(stderr, "Not linked. Run: shellbeats link <token>\n");
            return true;
        }

        printf("Following playlist %d... ", playlist_id);
        fflush(stdout);

        sb_sync_init();
        sb_push_result_t r = sb_follow_playlist(&cfg, playlist_id);
        sb_sync_cleanup();

        if (r.success) {
            printf("%s\n", r.error_msg);
        } else {
            printf("FAILED: %s\n", r.error_msg);
        }
        return true;
    }

    // ── shellbeats unfollow <playlist_id> ──
    if (strcmp(cmd, "unfollow") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Usage: shellbeats unfollow <playlist_id>\n");
            return true;
        }

        int playlist_id = atoi(argv[2]);
        if (playlist_id <= 0) {
            fprintf(stderr, "Error: invalid playlist ID\n");
            return true;
        }

        sb_sync_config_t cfg = sb_load_config(st->config_dir);
        if (strlen(cfg.token) == 0) {
            fprintf(stderr, "Not linked. Run: shellbeats link <token>\n");
            return true;
        }

        printf("Unfollowing playlist %d... ", playlist_id);
        fflush(stdout);

        sb_sync_init();
        sb_push_result_t r = sb_unfollow_playlist(&cfg, playlist_id);
        sb_sync_cleanup();

        if (r.success) {
            printf("%s\n", r.error_msg);
        } else {
            printf("FAILED: %s\n", r.error_msg);
        }
        return true;
    }

    return false; // Not a sync command
}

// ============================================================================
// Main
// ============================================================================

// Ensure JS runtimes (deno, node) are discoverable by yt-dlp.
// Users often install these in home-directory locations that may not be in
// the inherited PATH when shellbeats is launched from a desktop environment.
static void augment_path_for_js_runtimes(void) {
    const char *home = getenv("HOME");
    if (!home) return;

    const char *current_path = getenv("PATH");
    if (!current_path) current_path = "/usr/bin:/bin";

    // Common install locations for JS runtimes
    char shellbeats_bin[1024], deno_dir[1024], local_bin[1024];
    snprintf(shellbeats_bin, sizeof(shellbeats_bin), "%s/.shellbeats/bin", home);
    snprintf(deno_dir, sizeof(deno_dir), "%s/.deno/bin", home);
    snprintf(local_bin, sizeof(local_bin), "%s/.local/bin", home);

    // XDG config dir override
    const char *xdg_cfg = getenv("XDG_CONFIG_HOME");
    char xdg_shellbeats_bin[1024] = {0};
    if (xdg_cfg && xdg_cfg[0]) {
        snprintf(xdg_shellbeats_bin, sizeof(xdg_shellbeats_bin), "%s/shellbeats/bin", xdg_cfg);
    }

    // Also check for nvm-managed node
    char nvm_dir[1024];
    snprintf(nvm_dir, sizeof(nvm_dir), "%s/.nvm/versions/node", home);
    char nvm_node_bin[1024] = {0};
    if (dir_exists(nvm_dir)) {
        DIR *d = opendir(nvm_dir);
        if (d) {
            struct dirent *entry;
            char latest[256] = {0};
            int latest_major = -1, latest_minor = -1, latest_patch = -1;
            while ((entry = readdir(d)) != NULL) {
                if (entry->d_name[0] == 'v') {
                    int major = 0, minor = 0, patch = 0;
                    if (sscanf(entry->d_name, "v%d.%d.%d", &major, &minor, &patch) >= 1) {
                        if (major > latest_major ||
                            (major == latest_major && minor > latest_minor) ||
                            (major == latest_major && minor == latest_minor && patch > latest_patch)) {
                            latest_major = major;
                            latest_minor = minor;
                            latest_patch = patch;
                            snprintf(latest, sizeof(latest), "%s", entry->d_name);
                        }
                    }
                }
            }
            closedir(d);
            if (latest[0]) {
                snprintf(nvm_node_bin, sizeof(nvm_node_bin), "%s/%s/bin", nvm_dir, latest);
            }
        }
    }

    // Build augmented PATH (prepend so user installs take priority)
    size_t new_len = strlen(current_path) + strlen(shellbeats_bin) +
                     strlen(xdg_shellbeats_bin) + strlen(deno_dir) +
                     strlen(local_bin) + strlen(nvm_node_bin) + 32;
    char *new_path = malloc(new_len);
    if (!new_path) return;

    new_path[0] = '\0';
    // Highest priority: our own bin dir (where we install deno automatically)
    if (xdg_shellbeats_bin[0] && dir_exists(xdg_shellbeats_bin)) {
        strcat(new_path, xdg_shellbeats_bin);
        strcat(new_path, ":");
    }
    if (dir_exists(shellbeats_bin)) {
        strcat(new_path, shellbeats_bin);
        strcat(new_path, ":");
    }
    if (dir_exists(deno_dir)) {
        strcat(new_path, deno_dir);
        strcat(new_path, ":");
    }
    if (nvm_node_bin[0] && dir_exists(nvm_node_bin)) {
        strcat(new_path, nvm_node_bin);
        strcat(new_path, ":");
    }
    if (dir_exists(local_bin)) {
        strcat(new_path, local_bin);
        strcat(new_path, ":");
    }
    strcat(new_path, current_path);

    setenv("PATH", new_path, 1);
    free(new_path);
}

int main(int argc, char *argv[]) {
    setlocale(LC_ALL, "");

    // Ensure JS runtimes are findable by yt-dlp child processes
    augment_path_for_js_runtimes();

    // Initialize random seed for shuffle mode
    srand((unsigned int)time(NULL));

    // Check for -log flag
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-log") == 0 || strcmp(argv[i], "--log") == 0) {
            // Open log file early (before config dirs, use HOME directly)
            const char *home = getenv("HOME");
            if (!home) home = "/tmp";
            char log_path[1024];
            snprintf(log_path, sizeof(log_path), "%s/.shellbeats/shellbeats.log", home);
            // Ensure .shellbeats exists
            char config_dir[1024];
            snprintf(config_dir, sizeof(config_dir), "%s/.shellbeats", home);
            mkdir(config_dir, 0755);
            g_log_file = fopen(log_path, "a");
            if (g_log_file) {
                sb_log("========================================");
                sb_log("ShellBeats v%s started with -log", SHELLBEATS_VERSION);
                sb_log("HOME=%s", home);
            } else {
                fprintf(stderr, "Warning: could not open log file: %s\n", log_path);
            }
            break;
        }
    }

    AppState st = {0};
    st.playing_index = -1;
    st.playing_playlist_idx = -1;
    st.current_playlist_idx = -1;
    st.view = VIEW_SEARCH;

    // NEW: Initialize download queue mutex
    pthread_mutex_init(&st.download_queue.mutex, NULL);
    st.download_queue.current_idx = -1;
    g_app_state = &st;

    // Initialize config directories
    sb_log("Initializing config directories...");
    if (!init_config_dirs(&st)) {
        sb_log("FATAL: init_config_dirs failed");
        fprintf(stderr, "Failed to initialize config directory\n");
        return 1;
    }
    sb_log("Config dir: %s", st.config_dir);
    sb_log("yt-dlp bin dir: %s (exists=%s)", st.ytdlp_bin_dir, dir_exists(st.ytdlp_bin_dir) ? "yes" : "no");
    sb_log("yt-dlp local path: %s", st.ytdlp_local_path);
    
    // NEW: Load configuration
    load_config(&st);
    
    // Load playlists
    load_playlists(&st);

    // Handle sync CLI commands (link, unlink, sync, pull, share, unshare, status)
    // These run before ncurses and exit immediately after.
    if (handle_sync_commands(argc, argv, &st)) {
        free_all_playlists(&st);
        return 0;
    }

    // Version check (public, no auth needed — always runs)
    {
        sb_sync_init();
        sb_sync_config_t cfg = sb_load_config(st.config_dir);
        sb_check_latest_version(cfg.url, st.latest_version, sizeof(st.latest_version));
        sb_log("Version check: latest_version='%s' (local=%s)", st.latest_version, SHELLBEATS_VERSION);
        if (st.latest_version[0] != '\0') {
            int vcmp = version_compare(SHELLBEATS_VERSION, st.latest_version);
            sb_log("Version compare: local=%s remote=%s result=%d", SHELLBEATS_VERSION, st.latest_version, vcmp);
        } else {
            sb_log("Version check: no version received from server (url='%s')", cfg.url);
        }

        // SuriSync: startup hooks (pull_on_startup, status check)
        if (cfg.enabled && strlen(cfg.token) > 0) {
            sb_verify_result_t vr = sb_verify(&cfg);
            st.surikata_online = vr.success;

            if (vr.success && cfg.pull_on_startup) {
                sb_pull_result_t pr = sb_pull_all(&cfg);
                if (pr.success && pr.playlist_count > 0) {
                    write_pulled_playlists(&st, &pr);
                    load_playlists(&st);
                }
                sb_free_pull_result(&pr);

                // Check followed playlists for new songs (additive-only)
                sb_follow_check_result_t fc = sb_check_follows(&cfg);
                if (fc.success && fc.count > 0) {
                    int added = merge_followed_playlists(&st, &fc);
                    if (added > 0) {
                        load_playlists(&st);
                        fprintf(stderr, "SuriSync: %d new songs from %d followed playlists\n", added, fc.count);
                    }
                }
                sb_free_follow_check_result(&fc);
            }
        }

        sb_sync_cleanup();
    }

    // NEW: Load pending downloads from previous session
    load_download_queue(&st);

    // NEW: Start download thread if there are pending downloads
    if (get_pending_download_count(&st) > 0) {
        start_download_thread(&st);
    }

    // Start yt-dlp auto-update in background
    sb_log("Starting yt-dlp auto-update thread...");
    start_ytdlp_update(&st);
    start_deno_install(&st);

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    
    // Set timeout for non-blocking input (for poll-based event checking)
    timeout(100); // 100ms timeout
    
    char status[512] = "";
    
    if (!check_dependencies(&st, status, sizeof(status))) {
        draw_ui(&st, status);
        timeout(-1);
        getch();
        endwin();
        fprintf(stderr, "%s\n", status);
        return 1;
    }
    
    // Restore session if remember_session is enabled
    if (st.config.remember_session) {
        if (st.was_playing_playlist && st.last_playlist_idx >= 0 &&
            st.last_playlist_idx < st.playlist_count) {
            // Restore playlist view
            st.current_playlist_idx = st.last_playlist_idx;
            load_playlist_songs(&st, st.current_playlist_idx);
            if (st.last_song_idx >= 0 && st.last_song_idx < st.playlists[st.current_playlist_idx].count) {
                st.playlist_song_selected = st.last_song_idx;
            }
            st.view = VIEW_PLAYLIST_SONGS;
            snprintf(status, sizeof(status), "Resuming: %s, track %d",
                     st.playlists[st.current_playlist_idx].name,
                     st.last_song_idx + 1);
        } else if (st.cached_search_count > 0 && st.last_query[0]) {
            // Restore search results from cache
            for (int i = 0; i < st.cached_search_count && i < MAX_RESULTS; i++) {
                st.search_results[i] = st.cached_search[i];
                // Clear cached pointers so they won't be double-freed
                st.cached_search[i].title = NULL;
                st.cached_search[i].video_id = NULL;
                st.cached_search[i].url = NULL;
            }
            st.search_count = st.cached_search_count;
            snprintf(st.query, sizeof(st.query), "%s", st.last_query);
            if (st.last_song_idx >= 0 && st.last_song_idx < st.search_count) {
                st.search_selected = st.last_song_idx;
            }
            st.view = VIEW_SEARCH;
            snprintf(status, sizeof(status), "Resuming: search '%s', track %d",
                     st.query, st.last_song_idx + 1);
        } else {
            snprintf(status, sizeof(status), "Press / to search, d to download, f for playlists, h for help.");
        }
    } else {
        snprintf(status, sizeof(status), "Press / to search, d to download, f for playlists, h for help.");
    }
    draw_ui(&st, status);

    bool running = true;
    
    while (running) {
        // NEW: Update spinner for download animation
        time_t now = time(NULL);
        if (now != st.last_spinner_update) {
            st.spinner_frame++;
            st.last_spinner_update = now;
        }
        
        // Check for track end via mpv IPC
        // Only check if we've been playing for at least 3 seconds
        if (st.playing_index >= 0 && mpv_ipc_fd >= 0) {
            if (now - st.playback_started >= 3) {
                if (mpv_check_track_end()) {
                    // Auto-play next track
                    play_next(&st);
                    if (st.playing_index >= 0) {
                        const char *title = NULL;
                        if (st.playing_from_playlist && st.playing_playlist_idx >= 0) {
                            Playlist *pl = &st.playlists[st.playing_playlist_idx];
                            if (st.playing_index < pl->count) {
                                title = pl->items[st.playing_index].title;
                            }
                        } else if (st.playing_index < st.search_count) {
                            title = st.search_results[st.playing_index].title;
                        }
                        if (title) {
                            snprintf(status, sizeof(status), "Auto-playing: %s", title);
                        }
                    } else {
                        snprintf(status, sizeof(status), "Playback finished");
                    }
                    draw_ui(&st, status);
                }
            } else {
                // During grace period, still drain the socket buffer
                char drain_buf[4096];
                while (read(mpv_ipc_fd, drain_buf, sizeof(drain_buf)) > 0) {
                    // Discard data during grace period
                }
            }
        }
        
        int ch = getch();

        if (ch == ERR) {
            // Timeout - redraw UI to update spinner and download status
            draw_ui(&st, status);
            continue;
        }
        
        int rows, cols;
        getmaxyx(stdscr, rows, cols);
        (void)cols;
        int list_height = rows - 7;
        if (list_height < 1) list_height = 1;
        
        // NEW: Handle settings editing mode separately
        if (st.view == VIEW_SETTINGS && st.settings_editing) {
            switch (ch) {
                case 27: // Escape - cancel editing
                    st.settings_editing = false;
                    curs_set(0);
                    snprintf(status, sizeof(status), "Edit cancelled");
                    break;
                
                case '\n':
                case KEY_ENTER: // Save
                    if (st.settings_selected == 8) {
                        // Saving cookies file path
                        strncpy(st.config.yt_cookies_file, st.settings_edit_buffer,
                                sizeof(st.config.yt_cookies_file) - 1);
                        st.config.yt_cookies_file[sizeof(st.config.yt_cookies_file) - 1] = '\0';
                        save_config(&st);
                        st.settings_editing = false;
                        curs_set(0);
                        snprintf(status, sizeof(status), "Cookies file path saved");
                    } else {
                        // Default: download path (settings_selected == 0)
                        strncpy(st.config.download_path, st.settings_edit_buffer,
                                sizeof(st.config.download_path) - 1);
                        st.config.download_path[sizeof(st.config.download_path) - 1] = '\0';
                        save_config(&st);
                        st.settings_editing = false;
                        curs_set(0);
                        snprintf(status, sizeof(status), "Download path saved");
                    }
                    break;
                
                case KEY_BACKSPACE:
                case 127:
                case 8: // Backspace
                    if (st.settings_edit_pos > 0) {
                        memmove(&st.settings_edit_buffer[st.settings_edit_pos - 1],
                                &st.settings_edit_buffer[st.settings_edit_pos],
                                strlen(&st.settings_edit_buffer[st.settings_edit_pos]) + 1);
                        st.settings_edit_pos--;
                    }
                    break;
                
                case KEY_DC: // Delete
                    if (st.settings_edit_pos < (int)strlen(st.settings_edit_buffer)) {
                        memmove(&st.settings_edit_buffer[st.settings_edit_pos],
                                &st.settings_edit_buffer[st.settings_edit_pos + 1],
                                strlen(&st.settings_edit_buffer[st.settings_edit_pos + 1]) + 1);
                    }
                    break;
                
                case KEY_LEFT:
                    if (st.settings_edit_pos > 0) st.settings_edit_pos--;
                    break;
                
                case KEY_RIGHT:
                    if (st.settings_edit_pos < (int)strlen(st.settings_edit_buffer))
                        st.settings_edit_pos++;
                    break;
                
                case KEY_HOME:
                    st.settings_edit_pos = 0;
                    break;
                
                case KEY_END:
                    st.settings_edit_pos = strlen(st.settings_edit_buffer);
                    break;
                
                default:
                    // Insert printable character
                    if (ch >= 32 && ch < 127) {
                        int len = strlen(st.settings_edit_buffer);
                        if (len < (int)sizeof(st.settings_edit_buffer) - 1) {
                            memmove(&st.settings_edit_buffer[st.settings_edit_pos + 1],
                                    &st.settings_edit_buffer[st.settings_edit_pos],
                                    len - st.settings_edit_pos + 1);
                            st.settings_edit_buffer[st.settings_edit_pos] = ch;
                            st.settings_edit_pos++;
                        }
                    }
                    break;
            }
            draw_ui(&st, status);
            continue;
        }
        
        // Global keys
        switch (ch) {
            case 'q': {
                // NEW: Check for pending downloads before exiting
                int pending = get_pending_download_count(&st);
                if (pending > 0) {
                    draw_exit_dialog(&st, pending);
                    timeout(-1);
                    int confirm = getch();
                    timeout(100);
                    if (confirm == 'q') {
                        running = false;
                    }
                    // Otherwise continue (user cancelled)
                } else {
                    running = false;
                }
                continue;
            }
            
            case ' ':
                if (st.playing_index >= 0 && file_exists(IPC_SOCKET)) {
                    mpv_toggle_pause();
                    st.paused = !st.paused;
                    snprintf(status, sizeof(status), st.paused ? "Paused" : "Playing");
                }
                break;
            
            case 'n':
                if (st.playing_index >= 0) {
                    play_next(&st);
                    snprintf(status, sizeof(status), "Next track");
                }
                break;
            
            case 'p':
                if (st.playing_index >= 0) {
                    play_prev(&st);
                    snprintf(status, sizeof(status), "Previous track");
                }
                break;
            
            case 'h':
            case '?':
                show_help();
                break;

            case 'R': // Toggle shuffle mode
                st.shuffle_mode = !st.shuffle_mode;
                save_config(&st);
                snprintf(status, sizeof(status), "Shuffle: %s", st.shuffle_mode ? "ON" : "OFF");
                break;

            case 'L': // Cycle repeat mode
                st.repeat_mode = next_repeat_mode(st.repeat_mode);
                save_config(&st);
                snprintf(status, sizeof(status), "Repeat: %s", repeat_mode_label(st.repeat_mode));
                break;

            case KEY_LEFT:
                // Seek backward (only when not editing in settings)
                if (st.view != VIEW_SETTINGS && st.playing_index >= 0 && file_exists(IPC_SOCKET)) {
                    mpv_seek(-st.config.seek_step);
                    snprintf(status, sizeof(status), "<< -%ds", st.config.seek_step);
                }
                break;

            case KEY_RIGHT:
                // Seek forward (only when not editing in settings)
                if (st.view != VIEW_SETTINGS && st.playing_index >= 0 && file_exists(IPC_SOCKET)) {
                    mpv_seek(st.config.seek_step);
                    snprintf(status, sizeof(status), ">> +%ds", st.config.seek_step);
                }
                break;

            case 't': // Jump to time
                if (st.playing_index >= 0 && file_exists(IPC_SOCKET)) {
                    char time_input[16] = {0};
                    int len = get_string_input(time_input, sizeof(time_input), "Jump to (mm:ss): ");
                    if (len > 0) {
                        int mins = 0, secs = 0;
                        if (sscanf(time_input, "%d:%d", &mins, &secs) == 2 ||
                            sscanf(time_input, "%d", &secs) == 1) {
                            int total_secs = mins * 60 + secs;
                            mpv_seek_absolute(total_secs);
                            snprintf(status, sizeof(status), "Jump to %d:%02d", mins, secs);
                        } else {
                            snprintf(status, sizeof(status), "Invalid time format");
                        }
                    }
                }
                break;

            case 'i': // About
                st.view = VIEW_ABOUT;
                draw_ui(&st, status);
                timeout(-1);
                getch(); // Wait for any key
                timeout(100);
                st.view = VIEW_SEARCH;
                break;

            case 27: // Escape
                if (st.view == VIEW_PLAYLISTS) {
                    st.view = VIEW_SEARCH;
                    status[0] = '\0';
                } else if (st.view == VIEW_PLAYLIST_SONGS) {
                    st.view = VIEW_PLAYLISTS;
                    status[0] = '\0';
                } else if (st.view == VIEW_ADD_TO_PLAYLIST) {
                    st.view = VIEW_SEARCH;
                    st.song_to_add = NULL;
                    snprintf(status, sizeof(status), "Cancelled");
                } else if (st.view == VIEW_SETTINGS) {
                    st.view = VIEW_SEARCH;
                    status[0] = '\0';
                } else if (st.view == VIEW_ABOUT) {
                    st.view = VIEW_SEARCH;
                    status[0] = '\0';
                } else if (st.view == VIEW_SURISYNC) {
                    st.view = st.surisync_return_view;
                    status[0] = '\0';
                }
                break;
            
            case KEY_RESIZE:
                clear();
                break;
            
            default:
                break;
        }
        
        // View-specific keys
        switch (st.view) {
            case VIEW_SEARCH: {
                switch (ch) {
                    case KEY_UP:
                    case 'k':
                        if (st.search_selected > 0) st.search_selected--;
                        break;
                    
                    case KEY_DOWN:
                    case 'j':
                        if (st.search_selected + 1 < st.search_count) st.search_selected++;
                        break;
                    
                    case KEY_PPAGE:
                        st.search_selected -= list_height;
                        if (st.search_selected < 0) st.search_selected = 0;
                        break;
                    
                    case KEY_NPAGE:
                        st.search_selected += list_height;
                        if (st.search_selected >= st.search_count) 
                            st.search_selected = st.search_count - 1;
                        if (st.search_selected < 0) st.search_selected = 0;
                        break;
                    
                    case KEY_HOME:
                    case 'g':
                        st.search_selected = 0;
                        st.search_scroll = 0;
                        break;
                    
                    case KEY_END:
                        if (st.search_count > 0) {
                            st.search_selected = st.search_count - 1;
                        }
                        break;
                    
                    case '\n':
                    case KEY_ENTER:
                        if (st.search_count > 0) {
                            play_search_result(&st, st.search_selected);
                            snprintf(status, sizeof(status), "Playing: %s",
                                     st.search_results[st.search_selected].title ?
                                     st.search_results[st.search_selected].title : "?");
                        }
                        break;
                    
                    case '/':
                    case 's': {
                        char q[256] = {0};
                        int len = get_string_input(q, sizeof(q), "Search: ");
                        if (len > 0) {
                            snprintf(status, sizeof(status), "Searching: %s ...", q);
                            draw_ui(&st, status);
                            
                            int r = run_search(&st, q);
                            if (r < 0) {
                                snprintf(status, sizeof(status), "Search error!");
                            } else if (r == 0) {
                                if (st.yt_blocked) {
                                    snprintf(status, sizeof(status),
                                             "YouTube blocked the request. Press 'S' -> Cookies Mode to fix.");
                                } else {
                                    snprintf(status, sizeof(status), "No results for: %s", q);
                                }
                            } else {
                                snprintf(status, sizeof(status), "Found %d results for: %s", r, q);
                            }
                        } else {
                            snprintf(status, sizeof(status), "Search cancelled");
                        }
                        break;
                    }
                    
                    case 'x':
                        if (st.playing_index >= 0) {
                            mpv_stop_playback();
                            st.playing_index = -1;
                            st.playing_from_playlist = false;
                            st.playing_playlist_idx = -1;
                            st.paused = false;
                            snprintf(status, sizeof(status), "Playback stopped");
                        }
                        break;
                    
                    case 'f':
                        st.view = VIEW_PLAYLISTS;
                        st.playlist_selected = 0;
                        st.playlist_scroll = 0;
                        load_playlists(&st);
                        snprintf(status, sizeof(status), "Playlists");
                        break;
                    
                    case 'a':
                        if (st.search_count > 0) {
                            st.song_to_add = &st.search_results[st.search_selected];
                            st.add_to_playlist_selected = 0;
                            st.add_to_playlist_scroll = 0;
                            st.view = VIEW_ADD_TO_PLAYLIST;
                            snprintf(status, sizeof(status), "Select playlist");
                        } else {
                            snprintf(status, sizeof(status), "No song selected");
                        }
                        break;
                    
                    case 'c': {
                        char name[128] = {0};
                        int len = get_string_input(name, sizeof(name), "New playlist name: ");
                        if (len > 0) {
                            int idx = create_playlist(&st, name, false);
                            if (idx >= 0) {
                                snprintf(status, sizeof(status), "Created playlist: %s", name);
                            } else if (idx == -2) {
                                snprintf(status, sizeof(status), "Playlist already exists: %s", name);
                            } else {
                                snprintf(status, sizeof(status), "Failed to create playlist");
                            }
                        } else {
                            snprintf(status, sizeof(status), "Cancelled");
                        }
                        break;
                    }
                    
                    // NEW: Open settings with 'S'
                    case 'S':
                        st.view = VIEW_SETTINGS;
                        st.settings_selected = 0;
                        st.settings_editing = false;
                        snprintf(status, sizeof(status), "Settings");
                        break;
                    
                    // NEW: Download single song from search
                    case 'd':
                        if (st.search_count > 0) {
                            Song *song = &st.search_results[st.search_selected];
                            int result = add_to_download_queue(&st, song->video_id, song->title, NULL);
                            if (result > 0) {
                                snprintf(status, sizeof(status), "Queued: %s", song->title);
                            } else if (result == 0) {
                                snprintf(status, sizeof(status), "Already downloaded or queued");
                            } else {
                                snprintf(status, sizeof(status), "Failed to queue download");
                            }
                        } else {
                            snprintf(status, sizeof(status), "No song selected");
                        }
                        break;
                }
                break;
            }
            
            case VIEW_PLAYLISTS: {
                switch (ch) {
                    case KEY_UP:
                    case 'k':
                        if (st.playlist_selected > 0) st.playlist_selected--;
                        break;
                    
                    case KEY_DOWN:
                    case 'j':
                        if (st.playlist_selected + 1 < st.playlist_count) st.playlist_selected++;
                        break;
                    
                    case KEY_PPAGE:
                        st.playlist_selected -= list_height;
                        if (st.playlist_selected < 0) st.playlist_selected = 0;
                        break;
                    
                    case KEY_NPAGE:
                        st.playlist_selected += list_height;
                        if (st.playlist_selected >= st.playlist_count)
                            st.playlist_selected = st.playlist_count - 1;
                        if (st.playlist_selected < 0) st.playlist_selected = 0;
                        break;
                    
                    case '\n':
                    case KEY_ENTER:
                        if (st.playlist_count > 0) {
                            st.current_playlist_idx = st.playlist_selected;
                            load_playlist_songs(&st, st.current_playlist_idx);
                            st.playlist_song_selected = 0;
                            st.playlist_song_scroll = 0;
                            st.view = VIEW_PLAYLIST_SONGS;
                            snprintf(status, sizeof(status), "Opened: %s",
                                     st.playlists[st.current_playlist_idx].name);
                        }
                        break;
                    
                    case 'c': {
                        char name[128] = {0};
                        int len = get_string_input(name, sizeof(name), "New playlist name: ");
                        if (len > 0) {
                            int idx = create_playlist(&st, name, false);
                            if (idx >= 0) {
                                snprintf(status, sizeof(status), "Created playlist: %s", name);
                                st.playlist_selected = idx;
                            } else if (idx == -2) {
                                snprintf(status, sizeof(status), "Playlist already exists: %s", name);
                            } else {
                                snprintf(status, sizeof(status), "Failed to create playlist");
                            }
                        } else {
                            snprintf(status, sizeof(status), "Cancelled");
                        }
                        break;
                    }
                    
                    case 'x':
                        if (st.playlist_count > 0) {
                            char confirm[8] = {0};
                            char prompt[256];
                            snprintf(prompt, sizeof(prompt), "Delete '%s'? (y/n): ",
                                     st.playlists[st.playlist_selected].name);
                            get_string_input(confirm, sizeof(confirm), prompt);
                            if (confirm[0] == 'y' || confirm[0] == 'Y') {
                                if (delete_playlist(&st, st.playlist_selected)) {
                                    snprintf(status, sizeof(status), "Deleted playlist");
                                    if (st.playlist_selected >= st.playlist_count && st.playlist_count > 0) {
                                        st.playlist_selected = st.playlist_count - 1;
                                    }
                                } else {
                                    snprintf(status, sizeof(status), "Failed to delete");
                                }
                            } else {
                                snprintf(status, sizeof(status), "Cancelled");
                            }
                        }
                        break;

                    case 'e': // Rename playlist
                        if (st.playlist_count > 0) {
                            Playlist *pl = &st.playlists[st.playlist_selected];
                            char new_name[256] = {0};
                            char prompt[384];
                            snprintf(prompt, sizeof(prompt), "Rename '%s' to: ", pl->name);
                            int len = get_string_input(new_name, sizeof(new_name), prompt);
                            if (len > 0) {
                                // Check if name already exists
                                bool exists = false;
                                for (int i = 0; i < st.playlist_count; i++) {
                                    if (i != st.playlist_selected &&
                                        strcmp(st.playlists[i].name, new_name) == 0) {
                                        exists = true;
                                        break;
                                    }
                                }
                                if (exists) {
                                    snprintf(status, sizeof(status), "Playlist '%s' already exists", new_name);
                                } else {
                                    // Rename the playlist
                                    char old_json_path[4096], new_json_path[4096];
                                    char old_dl_path[4096], new_dl_path[4096];

                                    // Build old and new JSON file paths
                                    char old_filename[512], new_filename[512];
                                    snprintf(old_filename, sizeof(old_filename), "%s", pl->filename);
                                    // Create new filename from new name
                                    size_t fn_idx = 0;
                                    for (size_t i = 0; new_name[i] && fn_idx < sizeof(new_filename) - 6; i++) {
                                        char c = new_name[i];
                                        if (isalnum((unsigned char)c) || c == '-' || c == '_') {
                                            new_filename[fn_idx++] = c;
                                        } else if (c == ' ') {
                                            new_filename[fn_idx++] = '_';
                                        }
                                    }
                                    new_filename[fn_idx] = '\0';
                                    strcat(new_filename, ".json");

                                    snprintf(old_json_path, sizeof(old_json_path), "%s/%s",
                                             st.playlists_dir, old_filename);
                                    snprintf(new_json_path, sizeof(new_json_path), "%s/%s",
                                             st.playlists_dir, new_filename);

                                    // Build old and new download folder paths
                                    char old_safe[512], new_safe[512];
                                    playlist_dir_name(pl->name, old_safe, sizeof(old_safe));
                                    playlist_dir_name(new_name, new_safe, sizeof(new_safe));
                                    snprintf(old_dl_path, sizeof(old_dl_path), "%s/%s",
                                             st.config.download_path, old_safe);
                                    snprintf(new_dl_path, sizeof(new_dl_path), "%s/%s",
                                             st.config.download_path, new_safe);

                                    bool success = true;

                                    // Rename JSON file
                                    if (rename(old_json_path, new_json_path) != 0 && errno != ENOENT) {
                                        success = false;
                                    }

                                    // Rename download folder (if exists)
                                    if (success && dir_exists(old_dl_path)) {
                                        if (rename(old_dl_path, new_dl_path) != 0) {
                                            // Try to revert JSON rename
                                            rename(new_json_path, old_json_path);
                                            success = false;
                                        }
                                    }

                                    if (success) {
                                        // Update in-memory data
                                        free(pl->name);
                                        pl->name = strdup(new_name);
                                        free(pl->filename);
                                        pl->filename = strdup(new_filename);

                                        // Save updated index and playlist
                                        save_playlists_index(&st);
                                        save_playlist(&st, st.playlist_selected);

                                        snprintf(status, sizeof(status), "Renamed to '%s'", new_name);
                                    } else {
                                        snprintf(status, sizeof(status), "Failed to rename playlist");
                                    }
                                }
                            } else {
                                snprintf(status, sizeof(status), "Cancelled");
                            }
                        }
                        break;

                    // NEW: Add YouTube playlist
                    case 'p': {
                        char url[512] = {0};
                        int len = get_string_input(url, sizeof(url), "YouTube playlist URL: ");
                        if (len > 0) {
                            if (!validate_youtube_playlist_url(url)) {
                                snprintf(status, sizeof(status), "Invalid URL");
                                break;
                            }

                            snprintf(status, sizeof(status), "Validating URL...");
                            draw_ui(&st, status);

                            char fetched_title[256] = {0};
                            Song temp_songs[MAX_PLAYLIST_ITEMS];
                            char yt_cookie_args[1200];
                            build_cookie_args(&st, yt_cookie_args, sizeof(yt_cookie_args));
                            int fetched = fetch_youtube_playlist(url, temp_songs, MAX_PLAYLIST_ITEMS,
                                                                 fetched_title, sizeof(fetched_title),
                                                                 youtube_fetch_progress_callback, status,
                                                                 get_ytdlp_cmd(&st), yt_cookie_args);
                            if (fetched <= 0) {
                                snprintf(status, sizeof(status), "Failed to fetch playlist");
                                break;
                            }

                            char playlist_name[256];
                            int name_len = get_string_input(playlist_name, sizeof(playlist_name), "Playlist name: ");
                            if (name_len == 0) {
                                strncpy(playlist_name, fetched_title, sizeof(playlist_name) - 1);
                                playlist_name[sizeof(playlist_name) - 1] = '\0';
                            }

                            char mode[8] = {0};
                            while (1) {
                                get_string_input(mode, sizeof(mode), "Mode (s)tream or (d)ownload: ");
                                if (mode[0] == 's' || mode[0] == 'S' || mode[0] == 'd' || mode[0] == 'D') break;
                                snprintf(status, sizeof(status), "Invalid mode. Choose 's' or 'd'");
                                draw_ui(&st, status);
                            }
                            bool stream_only = (mode[0] == 's' || mode[0] == 'S');

                            int idx = create_playlist(&st, playlist_name, true);
                            if (idx < 0) {
                                snprintf(status, sizeof(status), "Failed to create playlist");
                                for (int i = 0; i < fetched; i++) {
                                    free(temp_songs[i].title);
                                    free(temp_songs[i].video_id);
                                    free(temp_songs[i].url);
                                }
                                break;
                            }

                            Playlist *pl = &st.playlists[idx];
                            if (pl->youtube_playlist_url) free(pl->youtube_playlist_url);
                            pl->youtube_playlist_url = strdup(url);
                            for (int i = 0; i < fetched; i++) {
                                pl->items[i] = temp_songs[i];
                                pl->count++;
                            }
                            save_playlist(&st, idx);

                            if (!stream_only) {
                                for (int i = 0; i < pl->count; i++) {
                                    add_to_download_queue(&st, pl->items[i].video_id, pl->items[i].title, pl->name);
                                }
                            }
                            status[0] = '\0';
                        } else {
                            snprintf(status, sizeof(status), "Cancelled");
                        }
                        break;
                    }
                    
                    // NEW: Download entire playlist
                    case 'd':
                        if (st.playlist_count > 0) {
                            Playlist *pl = &st.playlists[st.playlist_selected];
                            
                            // Make sure songs are loaded
                            if (pl->count == 0) {
                                load_playlist_songs(&st, st.playlist_selected);
                            }
                            
                            int added = 0;
                            int skipped = 0;
                            
                            for (int i = 0; i < pl->count; i++) {
                                int result = add_to_download_queue(&st, 
                                    pl->items[i].video_id,
                                    pl->items[i].title,
                                    pl->name);
                                
                                if (result > 0) {
                                    added++;
                                } else if (result == 0) {
                                    skipped++;
                                }
                            }
                            
                            if (added > 0) {
                                snprintf(status, sizeof(status), "Queued %d songs (%d already downloaded)", 
                                         added, skipped);
                            } else if (skipped > 0) {
                                snprintf(status, sizeof(status), "All %d songs already downloaded", skipped);
                            } else {
                                snprintf(status, sizeof(status), "Playlist is empty");
                            }
                        }
                        break;

                    // NEW: SuriSync overlay
                    case 's':
                        st.view = VIEW_SURISYNC;
                        st.surisync_selected = 0;
                        st.surisync_return_view = VIEW_PLAYLISTS;
                        status[0] = '\0';
                        break;
                }
                break;
            }

            case VIEW_PLAYLIST_SONGS: {
                Playlist *pl = NULL;
                if (st.current_playlist_idx >= 0 && st.current_playlist_idx < st.playlist_count) {
                    pl = &st.playlists[st.current_playlist_idx];
                }
                
                switch (ch) {
                    case KEY_UP:
                    case 'k':
                        if (st.playlist_song_selected > 0) st.playlist_song_selected--;
                        break;
                    
                    case KEY_DOWN:
                    case 'j':
                        if (pl && st.playlist_song_selected + 1 < pl->count) 
                            st.playlist_song_selected++;
                        break;
                    
                    case KEY_PPAGE:
                        st.playlist_song_selected -= list_height;
                        if (st.playlist_song_selected < 0) st.playlist_song_selected = 0;
                        break;
                    
                    case KEY_NPAGE:
                        if (pl) {
                            st.playlist_song_selected += list_height;
                            if (st.playlist_song_selected >= pl->count)
                                st.playlist_song_selected = pl->count - 1;
                            if (st.playlist_song_selected < 0) st.playlist_song_selected = 0;
                        }
                        break;
                    
                    case '\n':
                    case KEY_ENTER:
                        if (pl && pl->count > 0) {
                            play_playlist_song(&st, st.current_playlist_idx, st.playlist_song_selected);
                            snprintf(status, sizeof(status), "Playing: %s",
                                     pl->items[st.playlist_song_selected].title ?
                                     pl->items[st.playlist_song_selected].title : "?");
                        }
                        break;
                    
                    // NEW: Download single song from playlist (saves to playlist folder)
                    case 'd':
                        if (pl && pl->count > 0) {
                            Song *song = &pl->items[st.playlist_song_selected];
                            int result = add_to_download_queue(&st, song->video_id, song->title, pl->name);
                            if (result > 0) {
                                snprintf(status, sizeof(status), "Queued: %s", song->title);
                            } else if (result == 0) {
                                snprintf(status, sizeof(status), "Already downloaded or queued");
                            } else {
                                snprintf(status, sizeof(status), "Failed to queue download");
                            }
                        } else {
                            snprintf(status, sizeof(status), "No song selected");
                        }
                        break;
                    
                    // Add current song to another playlist
                    case 'a':
                        if (pl && pl->count > 0 && st.playlist_song_selected < pl->count) {
                            st.song_to_add = &pl->items[st.playlist_song_selected];
                            st.add_to_playlist_selected = 0;
                            st.view = VIEW_ADD_TO_PLAYLIST;
                        }
                        break;

                    // Remove song with 'X'
                    case 'X':
                        if (pl && pl->count > 0) {
                            char title_buf[256];
                            const char *title = pl->items[st.playlist_song_selected].title;
                            snprintf(title_buf, sizeof(title_buf), "%s", title ? title : "?");
                            if (remove_song_from_playlist(&st, st.current_playlist_idx,
                                                         st.playlist_song_selected)) {
                                snprintf(status, sizeof(status), "Removed: %s", title_buf);
                                if (st.playlist_song_selected >= pl->count && pl->count > 0) {
                                    st.playlist_song_selected = pl->count - 1;
                                }
                            } else {
                                snprintf(status, sizeof(status), "Failed to remove");
                            }
                        }
                        break;
                    
                    case 'D':
                        if (pl && pl->is_youtube_playlist && pl->count > 0) {
                            int added = 0;
                            for (int i = 0; i < pl->count; i++) {
                                int result = add_to_download_queue(&st, pl->items[i].video_id,
                                                                   pl->items[i].title, pl->name);
                                if (result > 0) added++;
                            }
                            if (added > 0) {
                                snprintf(status, sizeof(status), "Queued %d songs", added);
                            } else {
                                snprintf(status, sizeof(status), "All songs already queued or downloaded");
                            }
                        }
                        break;

                    case 'u': // Sync YouTube playlist
                        if (pl && pl->is_youtube_playlist) {
                            snprintf(status, sizeof(status), "Syncing playlist...");
                            draw_ui(&st, status);

                            char fetch_url[512] = {0};
                            int len = 0;
                            bool use_stored_url = false;

                            // Use stored YouTube playlist URL if available
                            if (pl->youtube_playlist_url && pl->youtube_playlist_url[0]) {
                                strncpy(fetch_url, pl->youtube_playlist_url, sizeof(fetch_url) - 1);
                                fetch_url[sizeof(fetch_url) - 1] = '\0';
                                len = strlen(fetch_url);
                                use_stored_url = true;
                            } else {
                                // Ask user to input URL if not stored
                                len = get_string_input(fetch_url, sizeof(fetch_url),
                                    "YouTube playlist URL to sync: ");
                                if (len == 0) {
                                    snprintf(status, sizeof(status), "Sync cancelled");
                                    break;
                                }
                            }

                            if (validate_youtube_playlist_url(fetch_url)) {
                                char fetched_title[256] = {0};
                                Song temp_songs[MAX_PLAYLIST_ITEMS];
                                char yt_cookie_args[1200];
                                build_cookie_args(&st, yt_cookie_args, sizeof(yt_cookie_args));
                                int fetched = fetch_youtube_playlist(fetch_url, temp_songs, MAX_PLAYLIST_ITEMS,
                                                                     fetched_title, sizeof(fetched_title),
                                                                     youtube_fetch_progress_callback, status,
                                                                     get_ytdlp_cmd(&st), yt_cookie_args);

                                if (fetched > 0) {
                                    // Count new songs (not already in playlist)
                                    int new_count = 0;
                                    int old_count = pl->count;

                                    for (int i = 0; i < fetched; i++) {
                                        bool exists = false;
                                        for (int j = 0; j < old_count; j++) {
                                            if (pl->items[j].video_id && temp_songs[i].video_id &&
                                                strcmp(pl->items[j].video_id, temp_songs[i].video_id) == 0) {
                                                exists = true;
                                                break;
                                            }
                                        }

                                        if (!exists && pl->count < MAX_PLAYLIST_ITEMS) {
                                            // Add new song
                                            int idx = pl->count;
                                            pl->items[idx].title = temp_songs[i].title;
                                            pl->items[idx].video_id = temp_songs[i].video_id;
                                            pl->items[idx].url = temp_songs[i].url;
                                            pl->items[idx].duration = temp_songs[i].duration;
                                            pl->count++;
                                            new_count++;
                                            // Clear pointers so they won't be freed below
                                            temp_songs[i].title = NULL;
                                            temp_songs[i].video_id = NULL;
                                            temp_songs[i].url = NULL;
                                        }
                                    }

                                    // Free remaining temp songs
                                    for (int i = 0; i < fetched; i++) {
                                        free(temp_songs[i].title);
                                        free(temp_songs[i].video_id);
                                        free(temp_songs[i].url);
                                    }

                                    if (pl->youtube_playlist_url) free(pl->youtube_playlist_url);
                                    pl->youtube_playlist_url = strdup(fetch_url);

                                    if (new_count > 0) {
                                        save_playlist(&st, st.current_playlist_idx);
                                        snprintf(status, sizeof(status), "Added %d new songs", new_count);
                                    } else {
                                        if (!use_stored_url) save_playlist(&st, st.current_playlist_idx);
                                        snprintf(status, sizeof(status), "Playlist is up to date");
                                    }
                                } else {
                                    snprintf(status, sizeof(status), "Failed to fetch playlist");
                                }
                            } else if (len > 0) {
                                snprintf(status, sizeof(status), "Invalid YouTube playlist URL");
                            } else {
                                snprintf(status, sizeof(status), "Sync cancelled");
                            }
                        } else {
                            snprintf(status, sizeof(status), "Not a YouTube playlist");
                        }
                        break;

                    case 'x':
                        if (st.playing_index >= 0) {
                            mpv_stop_playback();
                            st.playing_index = -1;
                            st.playing_from_playlist = false;
                            st.playing_playlist_idx = -1;
                            st.paused = false;
                            snprintf(status, sizeof(status), "Playback stopped");
                        }
                        break;

                    // NEW: SuriSync overlay
                    case 's':
                        st.view = VIEW_SURISYNC;
                        st.surisync_selected = 0;
                        st.surisync_return_view = VIEW_PLAYLIST_SONGS;
                        status[0] = '\0';
                        break;
                }
                break;
            }
            
            case VIEW_ADD_TO_PLAYLIST: {
                switch (ch) {
                    case KEY_UP:
                    case 'k':
                        if (st.add_to_playlist_selected > 0) st.add_to_playlist_selected--;
                        break;
                    
                    case KEY_DOWN:
                    case 'j':
                        if (st.add_to_playlist_selected + 1 < st.playlist_count)
                            st.add_to_playlist_selected++;
                        break;
                    
                    case '\n':
                    case KEY_ENTER:
                        if (st.playlist_count > 0 && st.song_to_add) {
                            if (add_song_to_playlist(&st, st.add_to_playlist_selected, st.song_to_add)) {
                                snprintf(status, sizeof(status), "Added: %s to: %s",
                                         st.song_to_add->title ? st.song_to_add->title : "?",
                                         st.playlists[st.add_to_playlist_selected].name);
                            } else {
                                snprintf(status, sizeof(status), "Already in playlist or failed");
                            }
                            st.song_to_add = NULL;
                            st.view = VIEW_SEARCH;
                        }
                        break;
                    
                    case 'c': {
                        char name[128] = {0};
                        int len = get_string_input(name, sizeof(name), "New playlist name: ");
                        if (len > 0) {
                            int idx = create_playlist(&st, name, false);
                            if (idx >= 0) {
                                if (st.song_to_add) {
                                    add_song_to_playlist(&st, idx, st.song_to_add);
                                    snprintf(status, sizeof(status), "Created '%s' and added song", name);
                                    st.song_to_add = NULL;
                                    st.view = VIEW_SEARCH;
                                } else {
                                    snprintf(status, sizeof(status), "Created: %s", name);
                                }
                            } else if (idx == -2) {
                                snprintf(status, sizeof(status), "Playlist already exists: %s", name);
                            } else {
                                snprintf(status, sizeof(status), "Failed to create playlist");
                            }
                        } else {
                            snprintf(status, sizeof(status), "Cancelled");
                        }
                        break;
                    }
                }
                break;
            }
            
            // NEW: Settings view key handling
            case VIEW_SETTINGS: {
                switch (ch) {
                    case KEY_UP:
                    case 'k':
                        if (st.settings_selected > 0) st.settings_selected--;
                        break;

                    case KEY_DOWN:
                    case 'j':
                        if (st.settings_selected < 10) st.settings_selected++;
                        break;

                    case '\n':
                    case KEY_ENTER:
                        if (st.settings_selected == 0) {
                            // Download path - enter edit mode
                            st.settings_editing = true;
                            strncpy(st.settings_edit_buffer, st.config.download_path,
                                    sizeof(st.settings_edit_buffer) - 1);
                            st.settings_edit_buffer[sizeof(st.settings_edit_buffer) - 1] = '\0';
                            st.settings_edit_pos = strlen(st.settings_edit_buffer);
                            snprintf(status, sizeof(status), "Editing download path...");
                        } else if (st.settings_selected == 1) {
                            // Seek step - prompt for new value
                            char step_input[16] = {0};
                            int len = get_string_input(step_input, sizeof(step_input), "Seek step (1-300 seconds): ");
                            if (len > 0) {
                                int new_step = atoi(step_input);
                                if (new_step >= 1 && new_step <= 300) {
                                    st.config.seek_step = new_step;
                                    save_config(&st);
                                    snprintf(status, sizeof(status), "Seek step set to %d seconds", new_step);
                                } else {
                                    snprintf(status, sizeof(status), "Invalid value (must be 1-300)");
                                }
                            }
                        } else if (st.settings_selected == 2) {
                            // Remember session - toggle
                            st.config.remember_session = !st.config.remember_session;
                            save_config(&st);
                            snprintf(status, sizeof(status), "Remember session: %s",
                                     st.config.remember_session ? "ON" : "OFF");
                        } else if (st.settings_selected == 3) {
                            // Shuffle mode - toggle
                            st.shuffle_mode = !st.shuffle_mode;
                            save_config(&st);
                            snprintf(status, sizeof(status), "Shuffle: %s",
                                     st.shuffle_mode ? "ON" : "OFF");
                        } else if (st.settings_selected == 4) {
                            // Repeat mode - cycle
                            st.repeat_mode = next_repeat_mode(st.repeat_mode);
                            save_config(&st);
                            snprintf(status, sizeof(status), "Repeat: %s",
                                     repeat_mode_label(st.repeat_mode));
                        } else if (st.settings_selected == 5) {
                            // Search results - prompt for new value
                            char res_input[16] = {0};
                            int len = get_string_input(res_input, sizeof(res_input), "Search results (10-150): ");
                            if (len > 0) {
                                int new_val = atoi(res_input);
                                if (new_val >= 10 && new_val <= 150) {
                                    st.config.max_results = new_val;
                                    save_config(&st);
                                    snprintf(status, sizeof(status), "Search results set to %d", new_val);
                                } else {
                                    snprintf(status, sizeof(status), "Invalid value (must be 10-150)");
                                }
                            }
                        } else if (st.settings_selected == 6) {
                            // Cookies mode - cycle off -> auto -> manual
                            st.config.yt_cookies_mode = (st.config.yt_cookies_mode + 1) % 3;
                            // Default browser when entering auto mode the first time
                            if (st.config.yt_cookies_mode == YT_COOKIES_AUTO &&
                                !st.config.yt_cookies_browser[0]) {
                                snprintf(st.config.yt_cookies_browser,
                                         sizeof(st.config.yt_cookies_browser),
                                         "firefox");
                            }
                            save_config(&st);
                            const char *lab = "Off";
                            if (st.config.yt_cookies_mode == YT_COOKIES_AUTO) lab = "Auto from browser";
                            else if (st.config.yt_cookies_mode == YT_COOKIES_MANUAL) lab = "Manual file";
                            snprintf(status, sizeof(status), "Cookies mode: %s", lab);
                        } else if (st.settings_selected == 7) {
                            // Browser - cycle through supported list
                            if (st.config.yt_cookies_mode != YT_COOKIES_AUTO) {
                                snprintf(status, sizeof(status), "Set Cookies Mode to 'Auto from browser' first");
                            } else {
                                static const char *browsers[] = {
                                    "firefox", "chrome", "chromium", "brave",
                                    "edge", "safari", "opera", "vivaldi"
                                };
                                int n = (int)(sizeof(browsers) / sizeof(browsers[0]));
                                int idx = 0;
                                for (int i = 0; i < n; i++) {
                                    if (strcmp(st.config.yt_cookies_browser, browsers[i]) == 0) {
                                        idx = i;
                                        break;
                                    }
                                }
                                idx = (idx + 1) % n;
                                snprintf(st.config.yt_cookies_browser,
                                         sizeof(st.config.yt_cookies_browser),
                                         "%s", browsers[idx]);
                                save_config(&st);
                                if (strcmp(browsers[idx], "safari") == 0) {
                                    snprintf(status, sizeof(status),
                                             "Browser: safari (may need 'Full Disk Access' for terminal in macOS Settings)");
                                } else {
                                    snprintf(status, sizeof(status), "Browser: %s", browsers[idx]);
                                }
                            }
                        } else if (st.settings_selected == 8) {
                            // Cookies file path - text input
                            if (st.config.yt_cookies_mode != YT_COOKIES_MANUAL) {
                                snprintf(status, sizeof(status), "Set Cookies Mode to 'Manual file' first");
                            } else {
                                st.settings_editing = true;
                                strncpy(st.settings_edit_buffer, st.config.yt_cookies_file,
                                        sizeof(st.settings_edit_buffer) - 1);
                                st.settings_edit_buffer[sizeof(st.settings_edit_buffer) - 1] = '\0';
                                st.settings_edit_pos = strlen(st.settings_edit_buffer);
                                snprintf(status, sizeof(status), "Editing cookies file path...");
                            }
                        } else if (st.settings_selected == 9) {
                            // Test cookies - run a real fetch
                            snprintf(status, sizeof(status), "Testing yt-dlp with current cookies config...");
                            draw_ui(&st, status);

                            char cookie_args[1200];
                            build_cookie_args(&st, cookie_args, sizeof(cookie_args));
                            // Test command:
                            //   --no-config: ignore any user yt-dlp config that may
                            //                force a specific format
                            //   --ignore-no-formats-error: continue even if no format
                            //                              matches (we only want metadata)
                            //   --dump-json: get all metadata as JSON
                            // We test with a known stable video (Rick Astley).
                            char test_cmd[2048];
                            snprintf(test_cmd, sizeof(test_cmd),
                                     "%s%s --no-config --ignore-no-formats-error "
                                     "--dump-json --no-warnings "
                                     "'https://www.youtube.com/watch?v=dQw4w9WgXcQ' 2>&1",
                                     get_ytdlp_cmd(&st), cookie_args);

                            sb_log("[COOKIES TEST] command: %s", test_cmd);

                            FILE *fp = popen(test_cmd, "r");
                            if (!fp) {
                                snprintf(status, sizeof(status), "Test failed: cannot run yt-dlp");
                                sb_log("[COOKIES TEST] popen failed: %s", strerror(errno));
                            } else {
                                // Read output (could be a long JSON)
                                char *full_out = NULL;
                                size_t cap = 0, total = 0;
                                char chunk[4096];
                                size_t n;
                                while ((n = fread(chunk, 1, sizeof(chunk), fp)) > 0) {
                                    if (total + n + 1 > cap) {
                                        cap = (cap == 0) ? 8192 : cap * 2;
                                        if (total + n + 1 > cap) cap = total + n + 1;
                                        char *nb = realloc(full_out, cap);
                                        if (!nb) break;
                                        full_out = nb;
                                    }
                                    memcpy(full_out + total, chunk, n);
                                    total += n;
                                }
                                if (full_out) full_out[total] = '\0';
                                int rc = pclose(fp);

                                sb_log("[COOKIES TEST] exit=%d, output_len=%zu", rc, total);
                                if (full_out && total > 0) {
                                    // Log only first 800 chars to avoid huge JSON dumps
                                    size_t log_len = total > 800 ? 800 : total;
                                    char *log_buf = malloc(log_len + 4);
                                    if (log_buf) {
                                        memcpy(log_buf, full_out, log_len);
                                        if (total > 800) {
                                            log_buf[log_len] = '.';
                                            log_buf[log_len+1] = '.';
                                            log_buf[log_len+2] = '.';
                                            log_buf[log_len+3] = '\0';
                                        } else {
                                            log_buf[log_len] = '\0';
                                        }
                                        sb_log("[COOKIES TEST] output: %s", log_buf);
                                        free(log_buf);
                                    }
                                }

                                // Find first error line for the status bar
                                char first_err[256] = {0};
                                if (full_out) {
                                    const char *err_pos = strstr(full_out, "ERROR");
                                    if (err_pos) {
                                        const char *eol = strchr(err_pos, '\n');
                                        size_t elen = eol ? (size_t)(eol - err_pos) : strlen(err_pos);
                                        if (elen > sizeof(first_err) - 1) elen = sizeof(first_err) - 1;
                                        memcpy(first_err, err_pos, elen);
                                        first_err[elen] = '\0';
                                    }
                                }

                                bool looks_like_json = (full_out && full_out[0] == '{' && strstr(full_out, "\"title\""));

                                if (rc == 0 && looks_like_json) {
                                    // Extract title from JSON for friendly message
                                    char title[256] = "(unknown)";
                                    const char *tp = strstr(full_out, "\"title\"");
                                    if (tp) {
                                        tp = strchr(tp, ':');
                                        if (tp) {
                                            tp++;
                                            while (*tp == ' ' || *tp == '\t') tp++;
                                            if (*tp == '"') {
                                                tp++;
                                                size_t i = 0;
                                                while (*tp && *tp != '"' && i < sizeof(title) - 1) {
                                                    if (*tp == '\\' && tp[1]) tp++;
                                                    title[i++] = *tp++;
                                                }
                                                title[i] = '\0';
                                            }
                                        }
                                    }
                                    snprintf(status, sizeof(status), "OK - fetched: %s", title);
                                } else if (full_out && strstr(full_out, "Sign in to confirm")) {
                                    snprintf(status, sizeof(status),
                                             "FAIL - YouTube bot check. Configure cookies above. Full log: ~/.shellbeats/shellbeats.log");
                                } else if (first_err[0]) {
                                    snprintf(status, sizeof(status),
                                             "FAIL - %s | full log: ~/.shellbeats/shellbeats.log",
                                             first_err);
                                } else {
                                    snprintf(status, sizeof(status),
                                             "FAIL - exit=%d. Check log: ~/.shellbeats/shellbeats.log",
                                             rc);
                                }
                                free(full_out);
                            }
                        } else if (st.settings_selected == 10) {
                            // Copy guide link to clipboard
                            const char *link = "https://surikata.app/g/9fa4af84829f";
                            if (copy_to_clipboard(link)) {
                                snprintf(status, sizeof(status), "Guide link copied to clipboard");
                            } else {
                                snprintf(status, sizeof(status),
                                         "No clipboard helper found. Copy this URL manually: %s", link);
                            }
                        }
                        break;
                }
                break;
            }

            case VIEW_ABOUT: {
                // About view doesn't handle any keys (just closes on any key)
                break;
            }

            case VIEW_SURISYNC: {
                switch (ch) {
                    case KEY_UP:
                    case 'k':
                        // Skip separator at index 7
                        if (st.surisync_selected == 8)
                            st.surisync_selected = 6;
                        else if (st.surisync_selected > 0)
                            st.surisync_selected--;
                        break;

                    case KEY_DOWN:
                    case 'j':
                        if (st.surisync_selected == 6)
                            st.surisync_selected = 8;
                        else if (st.surisync_selected < 9)
                            st.surisync_selected++;
                        break;

                    case '\n':
                    case KEY_ENTER: {
                        sb_sync_config_t cfg = sb_load_config(st.config_dir);
                        bool linked = (strlen(cfg.token) > 0);

                        switch (st.surisync_selected) {
                            case 0: { // Status
                                if (!linked) {
                                    snprintf(status, sizeof(status), "Not linked. Use 'Link token' first.");
                                } else {
                                    snprintf(status, sizeof(status), "Checking status...");
                                    draw_ui(&st, status);

                                    sb_sync_init();
                                    sb_verify_result_t vr = sb_verify(&cfg);
                                    sb_sync_cleanup();

                                    if (vr.success) {
                                        st.surikata_online = true;
                                        snprintf(status, sizeof(status),
                                                 "Online: %s (%d playlists synced)",
                                                 vr.username, vr.playlists_synced);
                                    } else {
                                        st.surikata_online = false;
                                        snprintf(status, sizeof(status), "Offline: %s", vr.error_msg);
                                    }
                                }
                                break;
                            }

                            case 1: { // Link token
                                char token[64] = {0};
                                int len = get_string_input(token, sizeof(token), "Token (sb_...): ");
                                if (len > 0) {
                                    if (strncmp(token, "sb_", 3) != 0 || strlen(token) != 43) {
                                        snprintf(status, sizeof(status), "Invalid token format (sb_ + 40 hex)");
                                    } else {
                                        snprintf(status, sizeof(status), "Verifying...");
                                        draw_ui(&st, status);

                                        snprintf(cfg.token, sizeof(cfg.token), "%s", token);
                                        if (strlen(cfg.url) == 0)
                                            strncpy(cfg.url, "https://surikata.app", sizeof(cfg.url) - 1);
                                        cfg.enabled = true;

                                        sb_sync_init();
                                        sb_verify_result_t vr = sb_verify(&cfg);
                                        sb_sync_cleanup();

                                        if (vr.success) {
                                            sb_save_config(st.config_dir, &cfg);
                                            st.surikata_online = true;
                                            snprintf(status, sizeof(status), "Linked as %s!", vr.username);
                                        } else {
                                            snprintf(status, sizeof(status), "Failed: %s", vr.error_msg);
                                        }
                                    }
                                } else {
                                    snprintf(status, sizeof(status), "Cancelled");
                                }
                                break;
                            }

                            case 2: { // Unlink
                                if (!linked) {
                                    snprintf(status, sizeof(status), "Not linked");
                                } else {
                                    memset(cfg.token, 0, sizeof(cfg.token));
                                    cfg.enabled = false;
                                    sb_save_config(st.config_dir, &cfg);
                                    st.surikata_online = false;
                                    snprintf(status, sizeof(status), "Unlinked from Surikata");
                                }
                                break;
                            }

                            case 3: { // Sync (push all)
                                if (!linked) {
                                    snprintf(status, sizeof(status), "Not linked");
                                } else {
                                    snprintf(status, sizeof(status), "Syncing all playlists...");
                                    draw_ui(&st, status);

                                    // Build array of all playlists
                                    sb_playlist_t *sbs = calloc(st.playlist_count, sizeof(sb_playlist_t));
                                    if (sbs) {
                                        for (int i = 0; i < st.playlist_count; i++) {
                                            if (st.playlists[i].count == 0)
                                                load_playlist_songs(&st, i);
                                            sbs[i] = playlist_to_sb(&st, i);
                                        }

                                        sb_sync_init();
                                        sb_push_result_t pr = sb_push_all(&cfg, sbs, st.playlist_count, NULL);
                                        sb_sync_cleanup();

                                        for (int i = 0; i < st.playlist_count; i++)
                                            sb_free_playlist(&sbs[i]);
                                        free(sbs);

                                        if (pr.success) {
                                            snprintf(status, sizeof(status),
                                                     "Synced %d playlists", pr.synced_count);
                                        } else {
                                            snprintf(status, sizeof(status), "Sync failed: %s", pr.error_msg);
                                        }
                                    }
                                }
                                break;
                            }

                            case 4: { // Pull (download all)
                                if (!linked) {
                                    snprintf(status, sizeof(status), "Not linked");
                                } else {
                                    snprintf(status, sizeof(status), "Pulling playlists...");
                                    draw_ui(&st, status);

                                    sb_sync_init();
                                    sb_pull_result_t pr = sb_pull_all(&cfg);
                                    sb_sync_cleanup();

                                    if (pr.success) {
                                        write_pulled_playlists(&st, &pr);
                                        load_playlists(&st);
                                        snprintf(status, sizeof(status),
                                                 "Pulled %d playlists", pr.playlist_count);
                                    } else {
                                        snprintf(status, sizeof(status), "Pull failed: %s", pr.error_msg);
                                    }
                                    sb_free_pull_result(&pr);
                                }
                                break;
                            }

                            case 5: { // Share current playlist
                                if (!linked) {
                                    snprintf(status, sizeof(status), "Not linked");
                                } else if (st.surisync_return_view != VIEW_PLAYLIST_SONGS ||
                                           st.current_playlist_idx < 0) {
                                    snprintf(status, sizeof(status), "Open a playlist first");
                                } else {
                                    int idx = st.current_playlist_idx;
                                    if (st.playlists[idx].is_shared) {
                                        snprintf(status, sizeof(status), "Already shared");
                                        break;
                                    }
                                    if (st.playlists[idx].count == 0)
                                        load_playlist_songs(&st, idx);

                                    // Set local flag first
                                    st.playlists[idx].is_shared = true;
                                    save_playlist(&st, idx);

                                    sb_playlist_t sb = playlist_to_sb(&st, idx);

                                    snprintf(status, sizeof(status), "Sharing '%s'...", sb.name);
                                    draw_ui(&st, status);

                                    sb_sync_init();
                                    sb_push_result_t pr = sb_push_playlist(&cfg, &sb);
                                    sb_sync_cleanup();
                                    sb_free_playlist(&sb);

                                    if (pr.success) {
                                        snprintf(status, sizeof(status), "Shared '%s'",
                                                 st.playlists[idx].name);
                                    } else {
                                        // Revert local on failure
                                        st.playlists[idx].is_shared = false;
                                        save_playlist(&st, idx);
                                        snprintf(status, sizeof(status), "Failed: %s", pr.error_msg);
                                    }
                                }
                                break;
                            }

                            case 6: { // Unshare current playlist
                                if (!linked) {
                                    snprintf(status, sizeof(status), "Not linked");
                                } else if (st.surisync_return_view != VIEW_PLAYLIST_SONGS ||
                                           st.current_playlist_idx < 0) {
                                    snprintf(status, sizeof(status), "Open a playlist first");
                                } else {
                                    int idx = st.current_playlist_idx;
                                    if (!st.playlists[idx].is_shared) {
                                        snprintf(status, sizeof(status), "Not shared");
                                        break;
                                    }
                                    if (st.playlists[idx].count == 0)
                                        load_playlist_songs(&st, idx);

                                    // Set local flag first
                                    st.playlists[idx].is_shared = false;
                                    save_playlist(&st, idx);

                                    sb_playlist_t sb = playlist_to_sb(&st, idx);

                                    snprintf(status, sizeof(status), "Unsharing '%s'...", sb.name);
                                    draw_ui(&st, status);

                                    sb_sync_init();
                                    sb_push_result_t pr = sb_push_playlist(&cfg, &sb);
                                    sb_sync_cleanup();
                                    sb_free_playlist(&sb);

                                    if (pr.success) {
                                        snprintf(status, sizeof(status), "Unshared '%s'",
                                                 st.playlists[idx].name);
                                    } else {
                                        // Revert local on failure
                                        st.playlists[idx].is_shared = true;
                                        save_playlist(&st, idx);
                                        snprintf(status, sizeof(status), "Failed: %s", pr.error_msg);
                                    }
                                }
                                break;
                            }

                            case 8: { // Toggle pull_on_startup
                                cfg.pull_on_startup = !cfg.pull_on_startup;
                                sb_save_config(st.config_dir, &cfg);
                                snprintf(status, sizeof(status), "Pull on startup: %s",
                                         cfg.pull_on_startup ? "ON" : "OFF");
                                break;
                            }

                            case 9: { // Toggle sync_on_quit
                                cfg.sync_on_quit = !cfg.sync_on_quit;
                                sb_save_config(st.config_dir, &cfg);
                                snprintf(status, sizeof(status), "Sync on quit: %s",
                                         cfg.sync_on_quit ? "ON" : "OFF");
                                break;
                            }
                        }
                        break;
                    }
                }
                break;
            }
        }

        draw_ui(&st, status);
    }

    // Save session state before exit
    if (st.config.remember_session) {
        st.was_playing_playlist = st.playing_from_playlist;
        if (st.playing_from_playlist) {
            st.last_playlist_idx = st.playing_playlist_idx;
            st.last_song_idx = st.playing_index;
        } else {
            st.last_playlist_idx = -1;
            st.last_song_idx = st.search_selected;
            // Cache current search results
            snprintf(st.last_query, sizeof(st.last_query), "%s", st.query);
            st.cached_search_count = st.search_count;
            for (int i = 0; i < st.search_count && i < MAX_RESULTS; i++) {
                // Free any existing cached data
                free(st.cached_search[i].title);
                free(st.cached_search[i].video_id);
                free(st.cached_search[i].url);
                // Copy current search results
                st.cached_search[i].title = st.search_results[i].title ? strdup(st.search_results[i].title) : NULL;
                st.cached_search[i].video_id = st.search_results[i].video_id ? strdup(st.search_results[i].video_id) : NULL;
                st.cached_search[i].url = st.search_results[i].url ? strdup(st.search_results[i].url) : NULL;
                st.cached_search[i].duration = st.search_results[i].duration;
            }
        }
        save_config(&st);
    }

    // NEW: Stop download thread
    stop_download_thread(&st);
    stop_ytdlp_update(&st);
    stop_deno_install(&st);
    pthread_mutex_destroy(&st.download_queue.mutex);

    endwin();

    // SuriSync: sync on quit (after endwin so output is visible)
    {
        sb_sync_config_t cfg = sb_load_config(st.config_dir);
        if (cfg.enabled && strlen(cfg.token) > 0 && cfg.sync_on_quit) {
            sb_playlist_t *sbs = calloc(st.playlist_count, sizeof(sb_playlist_t));
            if (sbs) {
                for (int i = 0; i < st.playlist_count; i++) {
                    if (st.playlists[i].count == 0)
                        load_playlist_songs(&st, i);
                    sbs[i] = playlist_to_sb(&st, i);
                }

                sb_sync_init();
                sb_push_result_t pr = sb_push_all(&cfg, sbs, st.playlist_count, NULL);
                sb_sync_cleanup();

                if (!pr.success) {
                    fprintf(stderr, "SuriSync: push failed: %s\n", pr.error_msg);
                }

                for (int i = 0; i < st.playlist_count; i++)
                    sb_free_playlist(&sbs[i]);
                free(sbs);
            }
        }
    }

    // Version update notice at exit
    if (st.latest_version[0] != '\0') {
        if (version_compare(SHELLBEATS_VERSION, st.latest_version) < 0) {
            fprintf(stderr, "NEW VERSION AVAILABLE: v%s (current: v%s)\n", st.latest_version, SHELLBEATS_VERSION);
        }
    }

    // Cleanup
    free_search_results(&st);
    free_all_playlists(&st);
    mpv_quit();

    sb_log("ShellBeats exiting normally");
    if (g_log_file) {
        fclose(g_log_file);
        g_log_file = NULL;
    }

    return 0;
}
