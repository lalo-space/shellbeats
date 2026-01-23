## Official website
https://shellbeats.com

[![Make a donation](https://www.paypalobjects.com/en_US/i/btn/btn_donateCC_LG.gif)](https://www.paypal.com/donate/?hosted_button_id=RFY5QFC6XDX5N)

## Updates

**v0.4** 
- Now you can download or stream an entire playlists from youtube just pasting the link on terminal, thanks to ***kathiravanbtm*** [**More details**](YOUTUBE_PLAYLIST_GUIDE.md)
- Some bugfixes.

# shellbeats V0.4

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

## Dependencies

- `mpv` - audio playback
- `yt-dlp` - YouTube search, streaming and downloading
- `ncurses` - terminal UI
- `pthread` - background downloads

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
> 🛈 This setup has not been personally tested by the author, but the community confirms there are no compilation issues.



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
| `r` | Remove song from playlist |
| `x` | Delete playlist (including folder & downloaded files) |
| `d` | Download song or entire playlist |

### Other

| Key | Action |
|-----|--------|
| `S` | Open settings (configure download path) |
| `i` | Show about screen |
| `h` or `?` | Show help |

## Features

- **Offline Mode**: Download songs and play them without internet
- **Smart Playback**: Automatically plays from disk when available
- **Background Downloads**: Keep using the app while downloads run
- **Visual Feedback**: `[D]` marker shows downloaded songs, spinner shows active downloads
- **Organized Storage**: Each playlist gets its own folder
- **Clean Deletion**: Removing a playlist deletes its folder and all files
- **Persistent Queue**: Resume interrupted downloads on restart
- **Duplicate Prevention**: Won't download the same song twice

## BUGS

No known bugs at the moment! If you find something please open an issue.

## TODO
Find a way to use an "AI agent" to find the music on Youtube and turn it into a Shellbeats playlist

## License

GPL-3.0 license
