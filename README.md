## Updates

**v0.2** - Playlist support is here!
- Press `f` to access playlists menu
- Create, delete, and manage your playlists
- Add songs from search results with `a`
- Auto-play: when a song ends, the next one starts automatically
- Everything saved in `~/.shellbeats/` so your playlists persist between sessions
- All UI now in English (finally cleaned up those Italian strings)
- 100% translated in english no more half italian words.

# shellbeats

![Demo](shellbeats.gif)

A terminal music player for Linux. Search YouTube and stream audio directly from your command line.

![shellbeats](screenshot.png)

## Why?

I wrote this because I use a tiling window manager and I got tired of:

- Managing clunky music player windows that break my workflow
- Keeping browser tabs open with YouTube eating up RAM
- Getting distracted by video recommendations when I just want to listen to music

shellbeats stays in the terminal where it belongs. Search, play, done.

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
└────────────────────────────────────────────────────────────────────────────┘
```

1. You search for something
2. yt-dlp fetches results from YouTube
3. mpv streams the audio (nothing saved to disk)
4. IPC socket handles communication between shellbeats and mpv

### Auto-play detection

The auto-play feature uses mpv's IPC socket to detect when a track ends. Here's the deal:

- shellbeats connects to mpv via a Unix socket (`/tmp/shellbeats_mpv.sock`)
- The main loop uses `poll()` to check for events without burning CPU cycles
- When mpv finishes a track, it sends an `end-file` event with `reason: eof`
- shellbeats catches this and automatically loads the next song

There's a small catch though: when you start a new song, mpv might fire some events during the initial buffering phase. To avoid false positives (like skipping through the whole playlist instantly), there's a 3-second grace period after starting playback where end-file events are ignored. The socket buffer gets drained during this time so stale events don't pile up.

It's not the most elegant solution, but it works reliably without hammering the CPU with constant status polling.

### Playlist storage

Playlists are stored as simple JSON files:

```
~/.shellbeats/
├── playlists.json          # index of all playlists
└── playlists/
    ├── chill_vibes.json    # individual playlist
    ├── workout.json
    └── ...
```

Each playlist file just contains the song title and YouTube video ID. When you play a song, shellbeats reconstructs the URL from the ID. Simple and easy to edit by hand if you ever need to.

## Dependencies

- `mpv` - audio playback
- `yt-dlp` - YouTube search and streaming
- `ncurses` - terminal UI

## Setup

Install dependencies:

```bash
# Debian/Ubuntu
sudo apt install mpv libncurses-dev yt-dlp

# Arch
sudo pacman -S mpv ncurses yt-dlp
```

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
| `d` | Remove song from playlist |
| `x` | Delete playlist (when in playlists view) |

## BUGS

- [ ] **Investigate rare playback issue**: Some searched songs don't start playing for some reason. Not sure yet if it's a yt-dlp thing, an mpv thing, or something in my code. Need to dig into the logs and figure out what's happening. If you hit this, try searching for the same song again or pick a different result.

- [ ] Creating a playlist while adding a song directly from search doesn’t fetch the number of items in each playlist.

## TODO

- [ ] Download MP3 files to disk, including support for downloading entire playlists.

- [ ] Prioritize local storage for playback; stream only when a track has not been downloaded.

- [ ] Use an external thread to manage the download queue, providing status signals such as active downloads, automatic resume after closing the program, and skipping files that fail to download.

- [ ] Provide customizable settings via an ncurses interface, with all settings saved to a configuration file.

- [ ] Allow users to edit the storage path for downloaded music.
      
## License

MIT - do whatever you want with it.
