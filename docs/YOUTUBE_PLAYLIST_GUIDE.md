# YouTube Playlist Integration - Implementation Guide

## Files Created
1. `youtube_playlist.h` - Header with function declarations
2. `youtube_playlist.c` - Implementation for fetching YouTube playlists

## Changes Required in shellbeats.c

### 1. Add Include (after line 21, after #include <dirent.h>)
```c
#include "youtube_playlist.h"
```

### 2. Update Playlist Struct (line 45-50)
Replace:
```c
typedef struct {
    char *name;
    char *filename;
    Song items[MAX_PLAYLIST_ITEMS];
    int count;
} Playlist;
```

With:
```c
typedef struct {
    char *name;
    char *filename;
    Song items[MAX_PLAYLIST_ITEMS];
    int count;
    bool is_youtube_playlist;
} Playlist;
```

### 3. Update create_playlist Function Signature
Find: `static int create_playlist(AppState *st, const char *name)`
Replace with: `static int create_playlist(AppState *st, const char *name, bool is_youtube)`

In the function body, add after `st->playlists[idx].count = 0;`:
```c
st->playlists[idx].is_youtube_playlist = is_youtube;
```

### 4. Update All create_playlist Calls
Find all calls to `create_playlist(&st, name)` and add `, false` parameter:
- Line ~2660: `int idx = create_playlist(&st, name, false);`
- Line ~2730: `int idx = create_playlist(&st, name, false);`
- Line ~3020: `int idx = create_playlist(&st, name, false);`

### 5. Update save_playlist Function
In the fprintf that writes the JSON, change:
```c
fprintf(f, "{\n  \"name\": \"%s\",\n  \"songs\": [\n", pl->name);
```
To:
```c
fprintf(f, "{\n  \"name\": \"%s\",\n  \"type\": \"%s\",\n  \"songs\": [\n",
        pl->name, pl->is_youtube_playlist ? "youtube" : "local");
```

### 6. Update load_playlist_songs Function
After parsing the name, add:
```c
char *type = json_get_string(content, "type");
pl->is_youtube_playlist = (type && strcmp(type, "youtube") == 0);
free(type);
```

### 7. Update play_playlist_song Function
Replace the file check logic with:
```c
mpv_start_if_needed();

if (pl->is_youtube_playlist) {
    mpv_load_url(pl->items[song_idx].url);
} else {
    char local_path[2048];
    if (get_local_file_path_for_song(st, pl->name, pl->items[song_idx].video_id,
                                      local_path, sizeof(local_path))) {
        mpv_load_url(local_path);
    } else {
        mpv_load_url(pl->items[song_idx].url);
    }
}
```

### 8. Update draw_playlist_songs_view Function
After printing the playlist name, add:
```c
printw("%s", pl->name);
if (pl->is_youtube_playlist) printw(" [YT]");
attroff(A_BOLD);
```

### 9. Update draw_header for VIEW_PLAYLISTS
Change:
```c
mvprintw(1, 0, "  Enter: open | d: download all | c: create | x: delete");
```
To:
```c
mvprintw(1, 0, "  Enter: open | d: download all | c: create | p: add YouTube | x: delete");
```

### 10. Update draw_header for VIEW_PLAYLIST_SONGS
Change the second line to:
```c
mvprintw(2, 0, "  a: add song | d: download | r: remove | D: download all (YT) | Esc: back | i: about | q: quit");
```

### 11. Add 'p' Key Handler in VIEW_PLAYLISTS (after case 'd':)
```c
case 'p': {
    char url[512] = {0};
    int len = get_string_input(url, sizeof(url), "YouTube playlist URL: ");
    if (len > 0) {
        if (!validate_youtube_playlist_url(url)) {
            snprintf(status, sizeof(status), "Invalid URL");
            break;
        }

        char fetched_title[256] = {0};
        Song temp_songs[MAX_PLAYLIST_ITEMS];
        int fetched = fetch_youtube_playlist(url, temp_songs, MAX_PLAYLIST_ITEMS, 
                                             fetched_title, sizeof(fetched_title));
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
```

### 12. Add 'D' Key Handler in VIEW_PLAYLIST_SONGS (after case 'r':)
```c
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
```

## Compilation
```bash
gcc shellbeats.c youtube_playlist.c -o shellbeats -lpthread -lncurses
```

Or update Makefile:
```makefile
shellbeats: shellbeats.c youtube_playlist.c
	gcc shellbeats.c youtube_playlist.c -o shellbeats -lpthread -lncurses
```

## Testing
1. Press 'f' to open playlists
2. Press 'p' to add YouTube playlist
3. Paste a YouTube playlist URL
4. **Watch the progress messages** as the playlist is fetched (shows "Fetching playlist info...", "Fetching songs...", "Fetched X songs...")
5. Enter playlist name (or press Enter to use fetched title)
6. Choose 's' for stream-only or 'd' to download
7. Open the playlist and press 'D' to download all songs later if needed

## Key Features
- URL validation for YouTube playlist links
- **Real-time progress display** during playlist fetching
- Fetches playlist title and songs via yt-dlp
- Stream-only mode (metadata saved, streams from YouTube)
- Download mode (queues all songs for download)
- [YT] indicator in UI for YouTube playlists
- 'D' key to download all songs in YouTube playlists
- Persistent storage of playlist type in JSON
