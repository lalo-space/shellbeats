## Official website
https://shellbeats.com

[![Make a donation](https://www.paypalobjects.com/en_US/i/btn/btn_donateCC_LG.gif)](https://www.paypal.com/donate/?hosted_button_id=RFY5QFC6XDX5N)

<a href="https://apps.apple.com/it/app/surikata/id6760357143"><img src="https://surikata.app/assets/stores/app-store.png" height="40"></a> **Surikata for playlist sync available in appstore!**

**We are looking for 12 betatester on surikata.app PlayStore App, if you are interested please sign-in and contact me on SurikataNews Guild!**

## Updates

I apologize for the absence I've been pouring every minute of my free time into the countless requests from users asking for "more tools like shellbeats." I'm about to officially go live with [surikata.app](https://surikata.app): it's a bit like what I always imagined the internet could be without the influence of big corps and marketing. It's a community built around maximum creative freedom and privacy. Through Surikata you can generate a token and keep a copy of your playlists, sync multiple PCs with the same playlists, and import (add-only) playlists shared by other users. To everyone who has appreciated shellbeats, I'm asking you to help me bring surikata.app to life we've submitted for publication on the App Store and Play Store, fingers crossed they approve us!

Previously: sorry for the delay on Unicode support doing it properly would require an additional dependency I'd prefer to avoid. I kindly ask for your patience on that front.

**v0.7**
- Fixed **pause state desync**: switching playlist and starting a new song while paused no longer causes inverted pause/play state. mpv is now explicitly unpaused on every `loadfile`.
- Added **XDG_CONFIG_HOME support**: if `$XDG_CONFIG_HOME/shellbeats` exists, it is used instead of `~/.shellbeats`. Existing setups are unaffected.
- Added **SuriSync** cloud sync integration: push/pull playlists to your Surikata account. Redesigned menu with contextual help text.
- Fixed **playlist path sanitization**: playlist names containing slashes or special characters no longer create broken directory paths.
- Changed **remove song** key to `X` (uppercase) to avoid conflicts with shuffle (`R`) and accidental deletions.
- Fixed compiler warnings: replaced all `strncpy` calls with `snprintf` for a clean zero-warning build.
- **New build dependencies**: `libcurl` and `cjson` are now required. If upgrading from a previous version, install them before building (see Setup section below).

**v0.6**
- Fixed song duration not showing (was displaying `--:--`). Duration now correctly fetched from yt-dlp. Actual playlists have duration null because it's stored on json during download.
- Added **Shuffle Mode** (`R` key): randomizes playback order within current search results or playlist. Loops infinitely until stopped. Shows `[SHUFFLE]` indicator in status bar.
- Added **Seek controls**: `Left/Right` arrow keys to seek backward/forward (default 10 seconds, configurable in Settings).
- Added **Jump to time** (`t` key): input `mm:ss` to jump to specific position in track.
- Added **Playlist rename** (`e` key): rename playlists including their download folders.
- Added **Remember Session**: optional setting to restore your last search/playlist on next startup. Caches search results locally.
- Added **YouTube playlist sync** (`u` key): update imported YouTube playlists with new songs added on YouTube.
- New Settings options: Seek Step (configurable), Remember Session (toggle), Shuffle Mode (toggle).

**v0.5**
- Fixed streaming on systems where mpv couldn't find yt-dlp: mpv now receives the correct yt-dlp path via `--script-opts=ytdl_hook-ytdl_path=...`, so streaming works even when yt-dlp is not in the system PATH.
- Added detailed playback logging (enabled with `-log` flag). All playback operations are now traced with `[PLAYBACK]` prefix: mpv startup, IPC connection, URL loading, search commands, track end detection, and stream errors. Useful for debugging playback issues on different systems.
- YouTube Playlist integration is now documented in this README (see below).
- Some bugfixes.

**v0.4**
- Now you can download or stream entire playlists from YouTube just by pasting the link in the terminal, thanks to ***kathiravanbtm***.
- Some bugfixes.

# shellbeats V0.7

![Demo](shellbeats.gif)

A terminal music player for Linux & macOS. Search YouTube, stream audio, and download your favorite tracks directly from your command line.

![shellbeats](shellbeats.jpg)

## Why?

I wrote this because I use a tiling window manager and I got tired of:

- Managing clunky music player windows that break my workflow
- Keeping browser tabs open with YouTube eating up RAM
- Getting distracted by video recommendations when I just want to listen to music
- Not having offline access to my favorite tracks

shellbeats stays in the terminal where it belongs. Search, play, download, done.

## How it works

```
┌────────────────────────────────────────────────────────────────────────────┐
│                              SHELLBEATS                                    │
│                                                                            │
│  ┌──────────┐      ┌──────────┐      ┌──────────┐      ┌──────────┐        │
│  │   TUI    │      │  yt-dlp  │      │   mpv    │      │  Audio   │        │
│  │Interface │ ───> │ (search) │      │ (player) │ ───> │  Output  │        │
│  └──────────┘      └──────────┘      └──────────┘      └──────────┘        │
│       │                 │                  ▲                               │
│       │                 │                  │                               │
│       │                 ▼                  │                               │
│       │           ┌──────────┐             │                               │
│       └─────────> │   IPC    │ ────────────┘                               │
│                   │  Socket  │                                             │
│                   └──────────┘                                             │
│                                                                            │
│  ┌──────────┐      ┌──────────┐      ┌──────────┐                          │
│  │ Download │      │  yt-dlp  │      │  Local   │                          │
│  │  Thread  │ ───> │ (extract)│ ───> │  Storage │                          │
│  └──────────┘      └──────────┘      └──────────┘                          │
└────────────────────────────────────────────────────────────────────────────┘
```

1. You search for something
2. yt-dlp fetches results from YouTube
3. mpv streams the audio (or plays from disk if downloaded)
4. IPC socket handles communication between shellbeats and mpv
5. Background thread processes download queue without blocking UI

### Download system

The download feature runs in a separate pthread to keep the UI responsive:

- Press `d` on any song to add it to the download queue
- Songs added to playlists are automatically queued for download
- Download happens in background you can keep browsing and playing music
- Queue persists to disk (`~/.shellbeats/download_queue.json`)
- If you quit with active downloads they'll resume next time you start shellbeats
- Files are organized by playlist: `~/Music/shellbeats/PlaylistName/Song_[videoid].mp3`
- Duplicate detection: won't download the same video twice
- Visual feedback: spinner in status bar shows active downloads

When playing from a playlist, shellbeats checks if the file exists locally first. If it does it plays from disk (instant, no buffering), otherwise it streams from YouTube.

### Auto-play detection

The auto-play feature uses mpv's IPC socket to detect when a track ends:

- shellbeats connects to mpv via a Unix socket (`/tmp/shellbeats_mpv.sock`)
- The main loop uses `getch()` with 100ms timeout to check for events without burning CPU
- When mpv finishes a track, it sends an `end-file` event with `reason: eof`
- shellbeats catches this and automatically loads the next song

There's a small catch: when you start a new song, mpv might fire some events during the initial buffering phase. To avoid false positives (like skipping through the whole playlist instantly), there's a 3-second grace period after starting playback where end-file events are ignored. The socket buffer gets drained during this time so stale events don't pile up.

### Playlist storage

shellbeats respects `$XDG_CONFIG_HOME` if set. If `$XDG_CONFIG_HOME/shellbeats` exists, it is used as the config directory. Otherwise, the default `~/.shellbeats` is used.

```
~/.shellbeats/                     (or $XDG_CONFIG_HOME/shellbeats/)
├── config.json             # app configuration (download path, settings)
├── playlists.json          # index of all playlists
├── download_queue.json     # pending downloads
├── shellbeats.log          # runtime log (when started with -log)
├── yt-dlp.version          # version of the local yt-dlp binary
├── bin/
│   └── yt-dlp              # auto-managed local yt-dlp binary
└── playlists/
    ├── chill_vibes.json    # individual playlist
    ├── workout.json
    └── ...
```

Downloaded files:

```
~/Music/shellbeats/
├── Rock Classics/
│   ├── Bohemian_Rhapsody_[dQw4w9WgXcQ].mp3
│   ├── Stairway_to_Heaven_[rn_YodiJO6k].mp3
│   └── ...
├── Jazz Favorites/
│   └── ...
└── (songs not in playlists go in root)
```

Each playlist file contains the song title and YouTube video ID. When you play a song, shellbeats reconstructs the URL from the ID. Simple and easy to edit by hand if you ever need to.

### Logging

Run shellbeats with the `-log` flag to enable detailed logging:

```bash
shellbeats -log
```

Logs are written to `~/.shellbeats/shellbeats.log`. All playback operations are traced with a `[PLAYBACK]` prefix, which makes it easy to filter:

```bash
tail -f ~/.shellbeats/shellbeats.log | grep PLAYBACK
```

What gets logged:

- **mpv lifecycle**: process start, IPC socket connection, disconnection, shutdown
- **Playback commands**: every command sent to mpv (loadfile, pause, stop)
- **URL loading**: which URL or local file is being loaded, and whether it's streaming or playing from disk
- **Search**: yt-dlp command executed, number of results found
- **Track navigation**: next/previous track, current index
- **Errors**: connection failures, stream errors (`end-file` with `reason: error`), socket issues

This is useful for debugging playback issues, especially on systems where streaming doesn't work. A typical failure looks like:

```
[PLAYBACK] mpv_check_track_end: WARNING - track ended with ERROR
```

which usually means mpv can't resolve the YouTube URL (yt-dlp not found, network issue, etc.).

## YouTube Playlist Integration

You can import entire YouTube playlists into shellbeats, either for streaming or for download.

### How to use

1. Press `f` to open the playlists menu
2. Press `p` to add a YouTube playlist
3. Paste a YouTube playlist URL (e.g. `https://www.youtube.com/playlist?list=PL...`)
4. Enter a name for the playlist (or press Enter to use the original YouTube title)
5. Choose mode: `s` to stream only, or `d` to download all songs

### YouTube playlist controls

| Key | Context | Action |
|-----|---------|--------|
| `p` | Playlists menu | Import a YouTube playlist |
| `u` | Inside a YouTube playlist | Sync with YouTube (fetch new songs) |
| `D` | Inside a YouTube playlist | Download all songs in the playlist |

- Imported playlists show a `[YT]` indicator in the UI
- In **stream mode**, songs play directly from YouTube (no disk usage)
- In **download mode**, all songs are queued for background download
- You can always download later by opening the playlist and pressing `D`
- Playlist type (youtube/local) is persisted in the JSON file

> YouTube Playlist integration contributed by ***kathiravanbtm***

## SuriSync — Sync & Share Playlists

![SuriSync](surikata_preview.gif)

**Sync your playlists across multiple computers** and share them with the community through [surikata.app](https://surikata.app).

### How it works

1. Create a free account on [surikata.app](https://surikata.app) and generate a sync token
2. In shellbeats, press `s` from the playlist view to open the SuriSync menu
3. Paste your token to link your account
4. **Push** your local playlists to the cloud
5. **Pull** your playlists on any other computer running shellbeats

### What you can do

- **Sync between computers**: keep the same playlists on your desktop, laptop, and any other machine push from one, pull from another
- **Share playlists with the community**: your playlists can be discovered by other Surikata users
- **Import playlists from others**: browse and import (add-only) playlists shared by the community directly into shellbeats
- **Follow playlists**: follow other users' playlists and receive updates when they add new songs

Sync is additive-only importing or pulling never removes your existing songs.

### SuriSync controls

| Key | Context | Action |
|-----|---------|--------|
| `s` | Playlist view | Open SuriSync menu |
| `Enter` | SuriSync menu | Execute selected action (push/pull/link/unlink) |

## Dependencies

- `mpv` - audio playback
- `yt-dlp` - YouTube search, streaming and downloading (auto-managed, see below)
- `ncurses` - terminal UI
- `pthread` - background downloads
- `curl` or `wget` - needed for yt-dlp auto-update (at least one must be installed)

### yt-dlp auto-update

shellbeats manages its own local copy of yt-dlp independently from the system one. At startup a background thread:

1. Checks the latest yt-dlp release on GitHub (via `curl`, or `wget` as fallback)
2. Compares it with the local version stored in `~/.shellbeats/yt-dlp.version`
3. If outdated (or missing), downloads the new binary to `~/.shellbeats/bin/yt-dlp` and marks it executable

When running commands (search, download, streaming), shellbeats uses the local binary if available, otherwise falls back to the system `yt-dlp`. This means the system-installed `yt-dlp` package is only needed as a safety net, shellbeats will keep itself up to date automatically as long as `curl` or `wget` is present.

## Setup

Install dependencies:

### Debian/Ubuntu
```bash
sudo apt install mpv libncurses-dev libcurl4-openssl-dev libcjson-dev yt-dlp curl
```
### Arch
```bash
sudo pacman -S mpv ncurses curl cjson yt-dlp
```
### Fedora/RHEL/CentOS
```bash
sudo dnf install mpv ncurses-devel libcurl-devel cJSON-devel yt-dlp curl
```
### macOS (via [Homebrew](https://brew.sh/))
```bash
brew install mpv yt-dlp cjson curl
```
> Note: macOS setup has not been personally tested by the author, but the community confirms there are no compilation issues. ncurses is included with Xcode Command Line Tools.

Build:

```bash
make
make install
```
Binary will be copied to `/usr/local/bin/`.

Run:

```bash
shellbeats
```

## Controls

All shortcuts are visible in the header when you run shellbeats. Here's the complete list:

### Playback

| Key | Action |
|-----|--------|
| `/` or `s` | Search YouTube |
| `Enter` | Play selected song |
| `Space` | Pause/Resume |
| `n` | Next track |
| `p` | Previous track |
| `x` | Stop playback |
| `R` | Toggle shuffle mode |
| `Left/Right` | Seek backward/forward |
| `t` | Jump to time (mm:ss) |
| `q` | Quit |

### Navigation

| Key | Action |
|-----|--------|
| `↑/↓` or `j/k` | Move selection |
| `PgUp/PgDn` | Page up/down |
| `g/G` | Jump to start/end |
| `Esc` | Go back |

### Playlists

| Key | Action |
|-----|--------|
| `f` | Open playlists menu |
| `a` | Add current song to a playlist |
| `c` | Create new playlist |
| `e` | Rename playlist |
| `p` | Import YouTube playlist |
| `X` | Remove song from playlist |
| `x` | Delete playlist (including folder & downloaded files) |
| `d` | Download song or entire playlist |
| `D` | Download all songs (YouTube playlists) |
| `u` | Sync YouTube playlist |

### SuriSync

| Key | Action |
|-----|--------|
| `s` | Open SuriSync menu (from playlist view) |

### Other

| Key | Action |
|-----|--------|
| `S` | Open settings |
| `i` | Show about screen |
| `h` or `?` | Show help |

### Settings

| Option | Description |
|--------|-------------|
| Download Path | Where downloaded songs are saved |
| Seek Step | Seconds to skip with Left/Right keys (default: 10) |
| Remember Session | Restore last search/playlist on startup |
| Shuffle Mode | Randomize playback order |

## Features

- **Offline Mode**: Download songs and play them without internet
- **Smart Playback**: Automatically plays from disk when available, streams otherwise
- **Background Downloads**: Keep using the app while downloads run
- **YouTube Playlists**: Import entire playlists for streaming or download
- **Shuffle Mode**: Randomize playback with infinite loop, shows `[SHUFFLE]` indicator
- **Seek Controls**: Jump forward/backward by configurable seconds, or to specific time
- **Session Memory**: Optionally restore your last search or playlist on startup
- **SuriSync**: Cloud sync playlists to your Surikata account
- **XDG Support**: Respects `$XDG_CONFIG_HOME` for config directory location
- **Visual Feedback**: `[D]` marker shows downloaded songs, `[YT]` marks YouTube playlists, spinner shows active downloads
- **Organized Storage**: Each playlist gets its own folder
- **Clean Deletion**: Removing a playlist deletes its folder and all files
- **Persistent Queue**: Resume interrupted downloads on restart
- **Duplicate Prevention**: Won't download the same song twice
- **Debug Logging**: Detailed playback logs with `-log` flag for troubleshooting

## BUGS

If you created a playlist in one of previous sessions, then when you save a track to the playlist, it displays the number of already saved tracks as (0).

## TODO

Add support for unicode characters (had some problems, was in 0.6 wishlist sorry).

Start buffering the next song in a separate process, then pause it so it's ready to resume immediately when the current track ends, reducing delay in music streaming.

Manage cookie from browser.

## Stores & Repo

[![badge](https://github.com/Botspot/pi-apps/blob/master/icons/badge.png?raw=true)](https://github.com/Botspot/pi-apps)

## License

GPL-3.0 license
