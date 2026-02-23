## Official website
https://shellbeats.com

[![Make a donation](https://www.paypalobjects.com/en_US/i/btn/btn_donateCC_LG.gif)](https://www.paypal.com/donate/?hosted_button_id=RFY5QFC6XDX5N)

👉 [Playing YouTube from the Command Line – HackaDay](https://hackaday.com/2026/01/31/playing-youtube-from-the-command-line/)

## Updates

Sorry for the delay. Unfortunately, I only had time over the weekend to work on refactoring and implementing new features. I then got stuck on a request to implement Unicode input/output support, but I ran into difficulties and realized that doing it properly would require introducing an additional dependency, which I would prefer to avoid. Therefore, I kindly ask for your patience regarding the Unicode support.

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

# shellbeats V0.6

![Demo](shellbeats.gif)

A terminal music player for Linux & OSX. Search YouTube, stream audio, and download your favorite tracks directly from your command line.

![shellbeats](screenshot.png)

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

The download feature runs in a seperate pthread to keep the UI responsive:

- Press `d` on any song to add it to the download queue
- Songs added to playlists are automatically queued for dowload
- Download happens in background - you can keep browsing and playing music
- Queue persists to disk (`~/.shellbeats/download_queue.json`)
- If you quit with active downloads they'll resume next time you start shellbeats
- Files are organized by playlist: `~/Music/shellbeats/PlaylistName/Song_[videoid].mp3`
- Duplicate detection: won't download the same video twice
- Visual feedback: spinner in status bar shows active downloads

When playing from a playlist, shellbeats checks if the file exists localy first. If it does it plays from disk (instant, no buffering), otherwise it streams from YouTube.

### Auto-play detection

The auto-play feature uses mpv's IPC socket to detect when a track ends. Here's the deal:

- shellbeats connects to mpv via a Unix socket (`/tmp/shellbeats_mpv.sock`)
- The main loop uses `getch()` with 100ms timeout to check for events without burning CPU
- When mpv finishes a track, it sends an `end-file` event with `reason: eof`
- shellbeats catches this and automatically loads the next song

There's a small catch though: when you start a new song, mpv might fire some events during the initial buffering phase. To avoid false positives (like skipping through the whole playlist instantly), there's a 3-second grace period after starting playback where end-file events are ignored. The socket buffer gets drained during this time so stale events don't pile up.

It's not the most elegant solution, but it works reliably without hammering the CPU with constant status polling.

### Playlist storage

Playlists are stored as simple JSON files:

```
~/.shellbeats/
├── config.json             # app configuration (download path)
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

Each playlist file just contains the song title and YouTube video ID. When you play a song shellbeats reconstructs the URL from the ID. Simple and easy to edit by hand if you ever need to.

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
| `D` | Inside a YouTube playlist | Download all songs in the playlist |

- Imported playlists show a `[YT]` indicator in the UI
- In **stream mode**, songs play directly from YouTube (no disk usage)
- In **download mode**, all songs are queued for background download
- You can always download later by opening the playlist and pressing `D`
- Playlist type (youtube/local) is persisted in the JSON file

> YouTube Playlist integration contributed by ***kathiravanbtm***

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

When running commands (search, download, streaming), shellbeats uses the local binary if available, otherwise falls back to the system `yt-dlp`. This means the system-installed `yt-dlp` package is only needed as a safety net — shellbeats will keep itself up to date automatically as long as `curl` or `wget` is present.

## Setup

Install dependencies:


### Debian/Ubuntu
```bash
sudo apt install mpv libncurses-dev yt-dlp
```
### Arch
```bash
sudo pacman -S mpv ncurses yt-dlp
```
### macOS (via [Homebrew](https://brew.sh/))
```bash
brew install mpv yt-dlp
```
> Note: This setup has not been personally tested by the author, but the community confirms there are no compilation issues.



Build:

```bash
make
make install
```
binary file will be copied in /usr/local/bin/

Run:

```bash
shellbeats
```

## Controls

All shortcuts are now visible in the header when you run shellbeats. Heres the complete list:

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
| `L` | Cycle repeat mode (OFF/ALL/ONE) |
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
| `r` | Remove song from playlist |
| `x` | Delete playlist (including folder & downloaded files) |
| `d` | Download song or entire playlist |
| `D` | Download all songs (YouTube playlists) |

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
| Repeat Mode | Playback repeat state: OFF, ALL, or ONE |

## Features

- **Offline Mode**: Download songs and play them without internet
- **Smart Playback**: Automatically plays from disk when available
- **Background Downloads**: Keep using the app while downloads run
- **YouTube Playlists**: Import entire playlists for streaming or download
- **Shuffle Mode**: Randomize playback with infinite loop, shows `[SHUFFLE]` indicator
- **Repeat Mode**: `ALL` loops queue/playlist, `ONE` repeats current track, shows `[REPEAT:*]`
- **Seek Controls**: Jump forward/backward by configurable seconds, or to specific time
- **Session Memory**: Optionally restore your last search or playlist on startup
- **Visual Feedback**: `[D]` marker shows downloaded songs, `[YT]` marks YouTube playlists, spinner shows active downloads
- **Organized Storage**: Each playlist gets its own folder
- **Clean Deletion**: Removing a playlist deletes its folder and all files
- **Persistent Queue**: Resume interrupted downloads on restart
- **Duplicate Prevention**: Won't download the same song twice
- **Debug Logging**: Detailed playback logs with `-log` flag for troubleshooting

## BUGS
If you created a playlist in one of previous sessions, then when you save a track to the playlist, it displays the number of already saved tracks as (0).
Small bug with PAUSE command tracking, sometimes the UI reverts the [PAUSE] message displayed.

## TODO
Find a way to use an "AI agent" to find the music on Youtube and turn it into a Shellbeats playlist.(probably not a goal of shellbeats)

Start buffering the next song in a separate process, then pause it so it’s ready to resume immediately when the current track ends, reducing delay in music streaming.

Manage cookie from browser.

Add support for unicode characters (had some problems was in 0.6 wishlist sorry).

Add a check for $XDG_CONFIG_HOME


## License

GPL-3.0 license
