# CHANGELOG

## v0.6
- Fixed song duration not showing (was displaying `--:--`). Duration now correctly fetched from yt-dlp. Actual playlists have duration null because it's stored on json during download.
- Added **Shuffle Mode** (`R` key): randomizes playback order within current search results or playlist. Loops infinitely until stopped. Shows `[SHUFFLE]` indicator in status bar.
- Added **Seek controls**: `Left/Right` arrow keys to seek backward/forward (default 10 seconds, configurable in Settings).
- Added **Jump to time** (`t` key): input `mm:ss` to jump to specific position in track.
- Added **Playlist rename** (`e` key): rename playlists including their download folders.
- Added **Remember Session**: optional setting to restore your last search/playlist on next startup. Caches search results locally.
- Added **YouTube playlist sync** (`u` key): update imported YouTube playlists with new songs added on YouTube.
- New Settings options: Seek Step (configurable), Remember Session (toggle), Shuffle Mode (toggle).

## v0.5
- Fixed streaming on systems where mpv couldn't find yt-dlp: mpv now receives the correct yt-dlp path via `--script-opts=ytdl_hook-ytdl_path=...`, so streaming works even when yt-dlp is not in the system PATH.
- Added detailed playback logging (enabled with `-log` flag). All playback operations are now traced with `[PLAYBACK]` prefix: mpv startup, IPC connection, URL loading, search commands, track end detection, and stream errors. Useful for debugging playback issues on different systems.
- YouTube Playlist integration is now documented in this README (see below).
- Some bugfixes.

## v0.4
- Now you can download or stream entire playlists from YouTube just by pasting the link in the terminal, thanks to ***kathiravanbtm***.
- Some bugfixes.
