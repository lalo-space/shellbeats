# shellbeats

![shellbeats](shellbeats.jpg)

A terminal music player for Linux. Search YouTube and stream audio directly from your command line.

Note: The code currently has some Italian strings mixed in. I started this project for personal use, then decided to publish it. Future updates will be fully in English.

![shellbeats](screenshot.png)


## Why?

I wrote this because I use a tiling window manager and I got tired of:
- Managing clunky music player windows that break my workflow
- Keeping browser tabs open with YouTube eating up RAM
- Getting distracted by video recommendations when I just want to listen to music

shellbeats stays in the terminal where it belongs. Search, play, done.

## How it works

```
┌──────────────┐      ┌──────────────┐      ┌──────────────┐
│  shellbeats  │ ───> │    yt-dlp    │ ───> │     mpv      │
│   (ncurses)  │      │   (search)   │      │  (playback)  │
└──────────────┘      └──────────────┘      └──────────────┘
```
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
```

Run:

```bash
./shellbeats
```

## Controls

| Key | Action |
|-----|--------|
| `/` | Search |
| `Enter` | Play |
| `Space` | Pause/Resume |
| `n` | Next |
| `p` | Previous |
| `↑/↓` or `j/k` | Navigate |
| `x` | Stop |
| `q` | Quit |
