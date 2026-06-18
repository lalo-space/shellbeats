#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "youtube_playlist.h"
#include "sb_exec.h"

/* Chunk size for yt-dlp --playlist-items requests. YouTube/yt-dlp can
 * silently cap --flat-playlist responses around 100 items for certain
 * playlist types (mixes, region-locked, paginated extractor output), so
 * we fetch in explicit ranges and accumulate until the playlist is
 * exhausted or max_songs is reached. */
#define CHUNK_SIZE 100

/* Fetch a single chunk of yt-dlp --flat-playlist output into songs[].
 * Returns the number of songs successfully parsed into the buffer,
 * 0 if the chunk produced no parseable lines (treat as end-of-playlist),
 * or -1 on hard error (failed to spawn yt-dlp). */
static int fetch_playlist_chunk(const char *url, const char *ytdlp_cmd,
                                const char *cookie_args,
                                Song *songs, int max_songs,
                                int chunk_start, int chunk_end) {
    char range_arg[32];
    snprintf(range_arg, sizeof(range_arg), "%d-%d", chunk_start, chunk_end);

    char *argv[18];
    int ac = 0;
    argv[ac++] = (char *)ytdlp_cmd;
    int cookie_from = ac;
    sb_parse_cookie_args(cookie_args, argv, &ac, 18);
    int cookie_to = ac;
    argv[ac++] = "--flat-playlist";
    argv[ac++] = "--quiet";
    argv[ac++] = "--no-warnings";
    argv[ac++] = "--playlist-items";
    argv[ac++] = range_arg;
    argv[ac++] = "--print";
    argv[ac++] = "%(title)s|||%(id)s|||%(duration)s";
    argv[ac++] = (char *)url;
    argv[ac] = NULL;

    FILE *fp = sb_exec_popen(argv, 0);
    sb_free_cookie_args(argv, cookie_from, cookie_to);
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
        } else {
            free(songs[count].title);
            free(songs[count].video_id);
            free(songs[count].url);
        }
    }

    free(line);
    sb_exec_pclose(fp);
    return count;
}

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

    /* --- Fetch playlist title (single yt-dlp call, only first item needed) --- */
    char *title_argv[18];
    int tac = 0;
    title_argv[tac++] = (char *)ytdlp_cmd;
    int cookie_from_t = tac;
    sb_parse_cookie_args(cookie_args, title_argv, &tac, 18);
    int cookie_to_t = tac;
    title_argv[tac++] = "--flat-playlist";
    title_argv[tac++] = "--quiet";
    title_argv[tac++] = "--no-warnings";
    title_argv[tac++] = "--playlist-items";
    title_argv[tac++] = "1";
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

    /* --- Fetch song list in chunks of CHUNK_SIZE --- */
    int total = 0;
    int chunk_start = 1;

    while (total < max_songs) {
        int chunk_end = chunk_start + CHUNK_SIZE - 1;
        int remaining = max_songs - total;
        int chunk_capacity = remaining < CHUNK_SIZE ? remaining : CHUNK_SIZE;

        int got = fetch_playlist_chunk(url, ytdlp_cmd, cookie_args,
                                       songs + total, chunk_capacity,
                                       chunk_start, chunk_end);
        if (got < 0) {
            /* Hard error spawning yt-dlp: keep what we have, bail out. */
            break;
        }
        if (got == 0) {
            /* No parseable items in this chunk → end of playlist. */
            break;
        }

        total += got;

        if (progress_callback) {
            char msg[128];
            snprintf(msg, sizeof(msg), "Fetched %d songs...", total);
            progress_callback(total, msg, callback_data);
        }

        /* Always advance by CHUNK_SIZE even if got < CHUNK_SIZE: a partial
         * chunk can happen when the playlist contains private/removed videos
         * in the middle. Trust the chunk_start counter; stop only when a
         * chunk is fully empty (got == 0 above). */
        chunk_start += CHUNK_SIZE;

        if (total >= max_songs) break;
    }

    // Report: Complete
    if (progress_callback && total > 0) {
        char msg[128];
        snprintf(msg, sizeof(msg), "Completed! Fetched %d songs", total);
        progress_callback(total, msg, callback_data);
    }

    return total;
}

bool validate_youtube_playlist_url(const char *url) {
    if (!url) return false;
    return (strstr(url, "youtube.com/playlist?list=") != NULL ||
            strstr(url, "youtu.be/playlist?list=") != NULL);
}
