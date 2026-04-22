#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "youtube_playlist.h"
#include "sb_exec.h"

int fetch_youtube_playlist(const char *url, Song *songs, int max_songs,
                           char *playlist_title, size_t title_size,
                           progress_callback_t progress_callback, void *callback_data,
                           const char *ytdlp_cmd, const char *cookie_args) {
    if (!url || !songs || max_songs <= 0 || !playlist_title || title_size == 0)
        return -1;

    if (!ytdlp_cmd || !ytdlp_cmd[0]) ytdlp_cmd = "yt-dlp";
    if (!cookie_args) cookie_args = "";

    /* Guard: validate_youtube_playlist_url() is the caller's job, but belt+braces.
     * Reject a URL that starts with a dash to avoid it being parsed as a flag. */
    if (url[0] == '-') return -1;

    // Report: Fetching playlist title
    if (progress_callback) {
        progress_callback(0, "Fetching playlist info...", callback_data);
    }

    /* --- Fetch playlist title --- */
    char *title_argv[16];
    int tac = 0;
    title_argv[tac++] = (char *)ytdlp_cmd;
    int cookie_from_t = tac;
    sb_parse_cookie_args(cookie_args, title_argv, &tac, 16);
    int cookie_to_t = tac;
    title_argv[tac++] = "--flat-playlist";
    title_argv[tac++] = "--quiet";
    title_argv[tac++] = "--no-warnings";
    title_argv[tac++] = "--print";
    title_argv[tac++] = "%(playlist_title)s";
    title_argv[tac++] = (char *)url;
    title_argv[tac] = NULL;

    FILE *title_fp = sb_exec_popen(title_argv, 0);
    sb_free_cookie_args(title_argv, cookie_from_t, cookie_to_t);
    if (!title_fp) return -1;

    char *title_line = NULL;
    size_t title_cap = 0;
    strcpy(playlist_title, "YouTube Playlist");

    if (getline(&title_line, &title_cap, title_fp) != -1) {
        size_t len = strlen(title_line);
        while (len > 0 && (title_line[len-1] == '\n' || title_line[len-1] == '\r'))
            title_line[--len] = '\0';
        if (len > 0) {
            strncpy(playlist_title, title_line, title_size - 1);
            playlist_title[title_size - 1] = '\0';
        }
    }
    free(title_line);
    sb_exec_pclose(title_fp);

    // Report: Fetching songs
    if (progress_callback) {
        progress_callback(0, "Fetching songs...", callback_data);
    }

    /* --- Fetch song list --- */
    char *list_argv[16];
    int lac = 0;
    list_argv[lac++] = (char *)ytdlp_cmd;
    int cookie_from_l = lac;
    sb_parse_cookie_args(cookie_args, list_argv, &lac, 16);
    int cookie_to_l = lac;
    list_argv[lac++] = "--flat-playlist";
    list_argv[lac++] = "--quiet";
    list_argv[lac++] = "--no-warnings";
    list_argv[lac++] = "--print";
    list_argv[lac++] = "%(title)s|||%(id)s|||%(duration)s";
    list_argv[lac++] = (char *)url;
    list_argv[lac] = NULL;

    FILE *fp = sb_exec_popen(list_argv, 0);
    sb_free_cookie_args(list_argv, cookie_from_l, cookie_to_l);
    if (!fp) return -1;

    char *line = NULL;
    size_t cap = 0;
    int count = 0;

    while (count < max_songs && getline(&line, &cap, fp) != -1) {
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
            line[--len] = '\0';

        if (!line[0] || strncmp(line, "ERROR", 5) == 0) continue;

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

        songs[count].title = strdup(title);
        songs[count].video_id = strdup(video_id);
        songs[count].url = malloc(256);
        if (songs[count].url) {
            snprintf(songs[count].url, 256, "https://www.youtube.com/watch?v=%s", video_id);
        }
        songs[count].duration = atoi(duration_str);

        if (songs[count].title && songs[count].video_id && songs[count].url) {
            count++;
            // Report progress every 10 songs
            if (progress_callback && (count % 10 == 0 || count == 1)) {
                char msg[128];
                snprintf(msg, sizeof(msg), "Fetched %d songs...", count);
                progress_callback(count, msg, callback_data);
            }
        } else {
            free(songs[count].title);
            free(songs[count].video_id);
            free(songs[count].url);
        }
    }

    free(line);
    sb_exec_pclose(fp);

    // Report: Complete
    if (progress_callback && count > 0) {
        char msg[128];
        snprintf(msg, sizeof(msg), "Completed! Fetched %d songs", count);
        progress_callback(count, msg, callback_data);
    }

    return count;
}

bool validate_youtube_playlist_url(const char *url) {
    if (!url) return false;
    return (strstr(url, "youtube.com/playlist?list=") != NULL ||
            strstr(url, "youtu.be/playlist?list=") != NULL);
}
