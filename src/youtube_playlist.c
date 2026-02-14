#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "youtube_playlist.h"

int fetch_youtube_playlist(const char *url, Song *songs, int max_songs,
                           char *playlist_title, size_t title_size,
                           progress_callback_t progress_callback, void *callback_data,
                           const char *ytdlp_cmd) {
    if (!url || !songs || max_songs <= 0 || !playlist_title || title_size == 0)
        return -1;

    if (!ytdlp_cmd || !ytdlp_cmd[0]) ytdlp_cmd = "yt-dlp";

    // Report: Fetching playlist title
    if (progress_callback) {
        progress_callback(0, "Fetching playlist info...", callback_data);
    }

    char title_cmd[2048];
    snprintf(title_cmd, sizeof(title_cmd),
             "%s --flat-playlist --quiet --no-warnings "
             "--print '%%(playlist_title)s' '%s' 2>/dev/null", ytdlp_cmd, url);

    FILE *title_fp = popen(title_cmd, "r");
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
    pclose(title_fp);

    // Report: Fetching songs
    if (progress_callback) {
        progress_callback(0, "Fetching songs...", callback_data);
    }

    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
             "%s --flat-playlist --quiet --no-warnings "
             "--print '%%(title)s|||%%(id)s|||%%(duration)s' "
             "'%s' 2>/dev/null", ytdlp_cmd, url);

    FILE *fp = popen(cmd, "r");
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
    pclose(fp);
    
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
