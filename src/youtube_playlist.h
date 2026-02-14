#ifndef YOUTUBE_PLAYLIST_H
#define YOUTUBE_PLAYLIST_H

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    char *title;
    char *video_id;
    char *url;
    int duration;
} Song;

// Callback function type for progress updates
// Parameters: current_count, message, user_data
typedef void (*progress_callback_t)(int current_count, const char *message, void *user_data);

int fetch_youtube_playlist(const char *url, Song *songs, int max_songs,
                           char *playlist_title, size_t title_size,
                           progress_callback_t progress_callback, void *callback_data,
                           const char *ytdlp_cmd);

bool validate_youtube_playlist_url(const char *url);

#endif
