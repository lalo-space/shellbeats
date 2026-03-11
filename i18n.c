#include <string.h>
#include "i18n.h"

Language current_language = LANG_EN;

static const char *strings_en[STR_COUNT] = {
    // Header
    [STR_TITLE] = " ShellBeats v0.6 ",

    // Header shortcuts - Search
    [STR_HDR_SEARCH_1] = "  /,s: search | Enter: play | Space: pause | n/p: next/prev | R: shuffle | t: jump",
    [STR_HDR_SEARCH_2] = "  Left/Right: seek | a: add | d: download | f: playlists | S: settings | q: quit",
    // Header shortcuts - Playlists
    [STR_HDR_PLAYLISTS_1] = "  Enter: open | c: create | e: rename | p: add YouTube | x: delete | d: download all",
    [STR_HDR_PLAYLISTS_2] = "  Esc: back | i: about | q: quit",
    // Header shortcuts - Playlist songs
    [STR_HDR_SONGS_1] = "  Enter: play | Space: pause | n/p: next/prev | R: shuffle | t: jump | Left/Right: seek",
    [STR_HDR_SONGS_2] = "  a: add | d: download | r: remove | D: download all | u: sync YT | Esc: back | q: quit",
    // Header shortcuts - Add to playlist
    [STR_HDR_ADD_1] = "  Enter: add to playlist | c: create new playlist",
    [STR_HDR_ADD_2] = "  Esc: cancel",
    // Header shortcuts - Settings
    [STR_HDR_SETTINGS_1] = "  Up/Down: navigate | Enter: edit/toggle",
    [STR_HDR_SETTINGS_2] = "  Esc: back | i: about | q: quit",
    // Header shortcuts - About
    [STR_HDR_ABOUT] = "  Press any key to close",

    // Download status
    [STR_FETCHING_UPDATES] = "Fetching updates...",

    // Now playing
    [STR_NOW_PLAYING] = " Now playing: ",
    [STR_PAUSED_TAG] = " [PAUSED]",
    [STR_SHUFFLE_TAG] = " [SHUFFLE]",

    // Search view
    [STR_QUERY] = "Query: ",
    [STR_QUERY_NONE] = "(none)",
    [STR_RESULTS_FMT] = "Results: %d",
    [STR_NO_TITLE] = "(no title)",
    [STR_DOWNLOADED_TAG] = "[D]",

    // Playlists view
    [STR_PLAYLISTS] = "Playlists",
    [STR_TOTAL_FMT] = "Total: %d",
    [STR_NO_PLAYLISTS] = "No playlists yet. Press 'c' to create one.",
    [STR_SONGS_FMT] = "%d songs",

    // Playlist songs view
    [STR_PLAYLIST_LABEL] = "Playlist: ",
    [STR_YT_TAG] = " [YT]",
    [STR_SONGS_COUNT_FMT] = "Songs: %d",
    [STR_PLAYLIST_EMPTY] = "Playlist is empty. Search for songs and press 'a' to add.",

    // Add to playlist view
    [STR_ADD_TO_PLAYLIST] = "Add to playlist: ",

    // Settings view
    [STR_SETTINGS] = "Settings",
    [STR_DOWNLOAD_PATH] = "Download Path:",
    [STR_SEEK_STEP_FMT] = "Seek Step (seconds): %d",
    [STR_REMEMBER_SESSION_FMT] = "Remember Session: %s",
    [STR_SHUFFLE_MODE_FMT] = "Shuffle Mode: %s",
    [STR_SETTINGS_HELP] = "Up/Down: navigate | Enter: edit/toggle | Esc: back",
    [STR_SETTINGS_EDIT_HELP] = "Editing: Enter to save, Esc to cancel",
    [STR_LANGUAGE_FMT] = "Language: %s",
    [STR_ON] = "ON",
    [STR_OFF] = "OFF",

    // Exit dialog
    [STR_DOWNLOAD_QUEUE] = " Download Queue ",
    [STR_DOWNLOADS_REMAINING_FMT] = "Downloads in progress: %d remaining",
    [STR_DOWNLOADS_RESUME] = "Downloads will resume on next startup.",
    [STR_EXIT_OPTIONS] = "[q] Quit anyway    [Esc] Cancel",

    // About view
    [STR_ABOUT_CREDIT] = "made by Lalo for Nami & Elia",
    [STR_ABOUT_DESC] = "A terminal-based music player for YouTube",
    [STR_ABOUT_FEATURES] = "Features:",
    [STR_ABOUT_FEAT1] = "* Search and stream music from YouTube",
    [STR_ABOUT_FEAT2] = "* Download songs as MP3",
    [STR_ABOUT_FEAT3] = "* Create and manage playlists",
    [STR_ABOUT_FEAT4] = "* Offline playback from local files",
    [STR_ABOUT_FOOTER] = "Built with mpv, yt-dlp, and ncurses",

    // Help screen
    [STR_HELP_TITLE] = "ShellBeats v0.6 | Help",
    [STR_HELP_PLAYBACK] = "PLAYBACK:",
    [STR_HELP_SEARCH] = "/,s         Search YouTube",
    [STR_HELP_PLAY] = "Enter       Play selected",
    [STR_HELP_PAUSE] = "Space       Pause/Resume",
    [STR_HELP_NEXTPREV] = "n/p         Next/Previous track",
    [STR_HELP_STOP] = "x           Stop playback",
    [STR_HELP_SHUFFLE] = "R           Toggle shuffle mode",
    [STR_HELP_SEEK] = "Left/Right  Seek backward/forward",
    [STR_HELP_JUMP] = "t           Jump to time (mm:ss)",
    [STR_HELP_NAV] = "NAVIGATION:",
    [STR_HELP_NAV_LIST] = "Up/Down/j/k Navigate list",
    [STR_HELP_NAV_PAGE] = "PgUp/PgDn   Page up/down",
    [STR_HELP_NAV_STARTEND] = "g/G         Go to start/end",
    [STR_HELP_NAV_BACK] = "Esc         Go back",
    [STR_HELP_PL] = "PLAYLISTS:",
    [STR_HELP_PL_OPEN] = "f           Open playlists menu",
    [STR_HELP_PL_ADD] = "a           Add song to playlist",
    [STR_HELP_PL_CREATE] = "c           Create new playlist",
    [STR_HELP_PL_RENAME] = "e           Rename playlist",
    [STR_HELP_PL_REMOVE] = "r           Remove song from playlist",
    [STR_HELP_PL_DL] = "d/D         Download song / Download all",
    [STR_HELP_PL_IMPORT] = "p           Import YouTube playlist",
    [STR_HELP_PL_SYNC] = "u           Sync YouTube playlist",
    [STR_HELP_PL_DELETE] = "x           Delete playlist",
    [STR_HELP_OTHER] = "OTHER:",
    [STR_HELP_SETTINGS] = "S           Settings",
    [STR_HELP_ABOUT] = "i           About",
    [STR_HELP_HELP] = "h,?         Show this help",
    [STR_HELP_QUIT] = "q           Quit",
    [STR_HELP_CONTINUE] = " Press any key to continue... ",

    // Input prompts
    [STR_PROMPT_JUMP] = "Jump to (mm:ss): ",
    [STR_PROMPT_SEARCH] = "Search: ",
    [STR_PROMPT_NEW_PLAYLIST] = "New playlist name: ",
    [STR_PROMPT_DELETE_FMT] = "Delete '%s'? (y/n): ",
    [STR_PROMPT_RENAME_FMT] = "Rename '%s' to: ",
    [STR_PROMPT_YT_URL] = "YouTube playlist URL: ",
    [STR_PROMPT_PL_NAME] = "Playlist name: ",
    [STR_PROMPT_MODE] = "Mode (s)tream or (d)ownload: ",
    [STR_PROMPT_SEEK_STEP] = "Seek step (1-300 seconds): ",
    [STR_PROMPT_YT_SYNC] = "YouTube playlist URL to sync: ",

    // Status messages
    [STR_STATUS_WELCOME] = "Press / to search, d to download, f for playlists, h for help.",
    [STR_STATUS_RESUMING_PL_FMT] = "Resuming: %s, track %d",
    [STR_STATUS_RESUMING_SEARCH_FMT] = "Resuming: search '%s', track %d",
    [STR_STATUS_AUTOPLAY_FMT] = "Auto-playing: %s",
    [STR_STATUS_FINISHED] = "Playback finished",
    [STR_STATUS_EDIT_CANCELLED] = "Edit cancelled",
    [STR_STATUS_PATH_SAVED] = "Download path saved",
    [STR_STATUS_PAUSED] = "Paused",
    [STR_STATUS_PLAYING] = "Playing",
    [STR_STATUS_NEXT] = "Next track",
    [STR_STATUS_PREV] = "Previous track",
    [STR_STATUS_SHUFFLE_FMT] = "Shuffle: %s",
    [STR_STATUS_SEEK_BACK_FMT] = "<< -%ds",
    [STR_STATUS_SEEK_FWD_FMT] = ">> +%ds",
    [STR_STATUS_JUMP_FMT] = "Jump to %d:%02d",
    [STR_STATUS_INVALID_TIME] = "Invalid time format",
    [STR_STATUS_CANCELLED] = "Cancelled",
    [STR_STATUS_PLAYING_FMT] = "Playing: %s",
    [STR_STATUS_SEARCHING_FMT] = "Searching: %s ...",
    [STR_STATUS_SEARCH_ERROR] = "Search error!",
    [STR_STATUS_NO_RESULTS_FMT] = "No results for: %s",
    [STR_STATUS_FOUND_FMT] = "Found %d results for: %s",
    [STR_STATUS_SEARCH_CANCELLED] = "Search cancelled",
    [STR_STATUS_STOPPED] = "Playback stopped",
    [STR_STATUS_SELECT_PLAYLIST] = "Select playlist",
    [STR_STATUS_NO_SONG] = "No song selected",
    [STR_STATUS_CREATED_FMT] = "Created playlist: %s",
    [STR_STATUS_EXISTS_FMT] = "Playlist already exists: %s",
    [STR_STATUS_CREATE_FAILED] = "Failed to create playlist",
    [STR_STATUS_EDITING_PATH] = "Editing download path...",
    [STR_STATUS_QUEUED_FMT] = "Queued: %s",
    [STR_STATUS_ALREADY_DL] = "Already downloaded or queued",
    [STR_STATUS_QUEUE_FAILED] = "Failed to queue download",
    [STR_STATUS_OPENED_FMT] = "Opened: %s",
    [STR_STATUS_DELETED] = "Deleted playlist",
    [STR_STATUS_DELETE_FAILED] = "Failed to delete",
    [STR_STATUS_RENAMED_FMT] = "Renamed to '%s'",
    [STR_STATUS_RENAME_FAILED] = "Failed to rename playlist",
    [STR_STATUS_RENAME_EXISTS_FMT] = "Playlist '%s' already exists",
    [STR_STATUS_INVALID_URL] = "Invalid URL",
    [STR_STATUS_VALIDATING] = "Validating URL...",
    [STR_STATUS_FETCH_FAILED] = "Failed to fetch playlist",
    [STR_STATUS_INVALID_MODE] = "Invalid mode. Choose 's' or 'd'",
    [STR_STATUS_QUEUED_BULK_FMT] = "Queued %d songs (%d already downloaded)",
    [STR_STATUS_ALL_DL_FMT] = "All %d songs already downloaded",
    [STR_STATUS_PL_EMPTY] = "Playlist is empty",
    [STR_STATUS_REMOVED_FMT] = "Removed: %s",
    [STR_STATUS_REMOVE_FAILED] = "Failed to remove",
    [STR_STATUS_QUEUED_SONGS_FMT] = "Queued %d songs",
    [STR_STATUS_ALL_QUEUED] = "All songs already queued or downloaded",
    [STR_STATUS_SYNCING] = "Syncing playlist...",
    [STR_STATUS_SYNC_ADDED_FMT] = "Added %d new songs",
    [STR_STATUS_SYNC_UPTODATE] = "Playlist is up to date",
    [STR_STATUS_SYNC_CANCELLED] = "Sync cancelled",
    [STR_STATUS_NOT_YT] = "Not a YouTube playlist",
    [STR_STATUS_INVALID_YT_URL] = "Invalid YouTube playlist URL",
    [STR_STATUS_ADDED_TO_FMT] = "Added to: %s",
    [STR_STATUS_ADD_FAILED] = "Already in playlist or failed",
    [STR_STATUS_CREATED_ADDED_FMT] = "Created '%s' and added song",
    [STR_STATUS_SEEK_SET_FMT] = "Seek step set to %d seconds",
    [STR_STATUS_SEEK_INVALID] = "Invalid value (must be 1-300)",
    [STR_STATUS_REMEMBER_FMT] = "Remember session: %s",

    // Dependency errors
    [STR_DEP_YTDLP] = "yt-dlp not found! Will be downloaded automatically on next start.",
    [STR_DEP_MPV] = "mpv not found! Install with: apt install mpv",

    // Duration placeholder
    [STR_DURATION_UNKNOWN] = "--:--",

    // Progress callbacks
    [STR_FETCHING_INFO] = "Fetching playlist info...",
    [STR_FETCHING_SONGS] = "Fetching songs...",
    [STR_FETCHED_FMT] = "Fetched %d songs...",
    [STR_FETCH_COMPLETE_FMT] = "Completed! Fetched %d songs",
};

static const char *strings_hu[STR_COUNT] = {
    // Header
    [STR_TITLE] = " ShellBeats v0.6 ",

    // Header shortcuts - Search
    [STR_HDR_SEARCH_1] = "  /,s: kereses | Enter: lejatszas | Space: szunet | n/p: kov./elozo | R: keveres | t: ugras",
    [STR_HDR_SEARCH_2] = "  Bal/Jobb: tekeres | a: hozzaadas | d: letoltes | f: listak | S: beallitasok | q: kilepes",
    // Header shortcuts - Playlists
    [STR_HDR_PLAYLISTS_1] = "  Enter: megnyitas | c: letrehozas | e: atnevezes | p: YouTube hozzaadasa | x: torles | d: osszes letoltese",
    [STR_HDR_PLAYLISTS_2] = "  Esc: vissza | i: nevjegy | q: kilepes",
    // Header shortcuts - Playlist songs
    [STR_HDR_SONGS_1] = "  Enter: lejatszas | Space: szunet | n/p: kov./elozo | R: keveres | t: ugras | Bal/Jobb: tekeres",
    [STR_HDR_SONGS_2] = "  a: hozzaadas | d: letoltes | r: eltavolitas | D: osszes letoltese | u: YT szinkron | Esc: vissza | q: kilepes",
    // Header shortcuts - Add to playlist
    [STR_HDR_ADD_1] = "  Enter: hozzaadas listahoz | c: uj lista letrehozasa",
    [STR_HDR_ADD_2] = "  Esc: megse",
    // Header shortcuts - Settings
    [STR_HDR_SETTINGS_1] = "  Fel/Le: navigacio | Enter: szerkesztes/valtas",
    [STR_HDR_SETTINGS_2] = "  Esc: vissza | i: nevjegy | q: kilepes",
    // Header shortcuts - About
    [STR_HDR_ABOUT] = "  Nyomj egy billentyut a bezarashoz",

    // Download status
    [STR_FETCHING_UPDATES] = "Frissitesek keresese...",

    // Now playing
    [STR_NOW_PLAYING] = " Most szol: ",
    [STR_PAUSED_TAG] = " [SZUNET]",
    [STR_SHUFFLE_TAG] = " [KEVERES]",

    // Search view
    [STR_QUERY] = "Kereses: ",
    [STR_QUERY_NONE] = "(nincs)",
    [STR_RESULTS_FMT] = "Talalatok: %d",
    [STR_NO_TITLE] = "(nincs cim)",
    [STR_DOWNLOADED_TAG] = "[L]",

    // Playlists view
    [STR_PLAYLISTS] = "Lejatszasi listak",
    [STR_TOTAL_FMT] = "Osszes: %d",
    [STR_NO_PLAYLISTS] = "Meg nincsenek listak. Nyomd meg a 'c'-t egy ujhoz.",
    [STR_SONGS_FMT] = "%d dal",

    // Playlist songs view
    [STR_PLAYLIST_LABEL] = "Lista: ",
    [STR_YT_TAG] = " [YT]",
    [STR_SONGS_COUNT_FMT] = "Dalok: %d",
    [STR_PLAYLIST_EMPTY] = "A lista ures. Keress dalokat es nyomd meg az 'a'-t.",

    // Add to playlist view
    [STR_ADD_TO_PLAYLIST] = "Hozzaadas listahoz: ",

    // Settings view
    [STR_SETTINGS] = "Beallitasok",
    [STR_DOWNLOAD_PATH] = "Letoltesi mappa:",
    [STR_SEEK_STEP_FMT] = "Tekeres lepese (mp): %d",
    [STR_REMEMBER_SESSION_FMT] = "Munkamenet megjegyzese: %s",
    [STR_SHUFFLE_MODE_FMT] = "Keveres mod: %s",
    [STR_SETTINGS_HELP] = "Fel/Le: navigacio | Enter: szerkesztes/valtas | Esc: vissza",
    [STR_SETTINGS_EDIT_HELP] = "Szerkesztes: Enter a menteshez, Esc a megsemisiteshez",
    [STR_LANGUAGE_FMT] = "Nyelv: %s",
    [STR_ON] = "BE",
    [STR_OFF] = "KI",

    // Exit dialog
    [STR_DOWNLOAD_QUEUE] = " Letoltesi sor ",
    [STR_DOWNLOADS_REMAINING_FMT] = "Letoltesek folyamatban: %d hatralevo",
    [STR_DOWNLOADS_RESUME] = "A letoltesek a kovetkezo inditaskor folytatodnak.",
    [STR_EXIT_OPTIONS] = "[q] Kilepes   [Esc] Megse",

    // About view
    [STR_ABOUT_CREDIT] = "keszitette Lalo, Naminak es Elianak",
    [STR_ABOUT_DESC] = "Terminalalapú zenelejatszo a YouTube-hoz",
    [STR_ABOUT_FEATURES] = "Funkciok:",
    [STR_ABOUT_FEAT1] = "* Kereses es streameles a YouTube-rol",
    [STR_ABOUT_FEAT2] = "* Dalok letoltese MP3-kent",
    [STR_ABOUT_FEAT3] = "* Lejatszasi listak kezelese",
    [STR_ABOUT_FEAT4] = "* Offline lejatszas helyi fajlokbol",
    [STR_ABOUT_FOOTER] = "Keszult mpv, yt-dlp es ncurses segitsegevel",

    // Help screen
    [STR_HELP_TITLE] = "ShellBeats v0.6 | Sugo",
    [STR_HELP_PLAYBACK] = "LEJATSZAS:",
    [STR_HELP_SEARCH] = "/,s         Kereses a YouTube-on",
    [STR_HELP_PLAY] = "Enter       Kivalasztott lejatszasa",
    [STR_HELP_PAUSE] = "Space       Szunet/Folytatas",
    [STR_HELP_NEXTPREV] = "n/p         Kovetkezo/Elozo szam",
    [STR_HELP_STOP] = "x           Lejatszas leallitasa",
    [STR_HELP_SHUFFLE] = "R           Keveres be/ki",
    [STR_HELP_SEEK] = "Bal/Jobb    Tekeres hatra/elore",
    [STR_HELP_JUMP] = "t           Ugras idopontra (pp:mm)",
    [STR_HELP_NAV] = "NAVIGACIO:",
    [STR_HELP_NAV_LIST] = "Fel/Le/j/k  Lista navigacio",
    [STR_HELP_NAV_PAGE] = "PgUp/PgDn   Lapozas fel/le",
    [STR_HELP_NAV_STARTEND] = "g/G         Ugras az elejere/vegere",
    [STR_HELP_NAV_BACK] = "Esc         Vissza",
    [STR_HELP_PL] = "LEJATSZASI LISTAK:",
    [STR_HELP_PL_OPEN] = "f           Listak megnyitasa",
    [STR_HELP_PL_ADD] = "a           Dal hozzaadasa listahoz",
    [STR_HELP_PL_CREATE] = "c           Uj lista letrehozasa",
    [STR_HELP_PL_RENAME] = "e           Lista atnevezese",
    [STR_HELP_PL_REMOVE] = "r           Dal eltavolitasa",
    [STR_HELP_PL_DL] = "d/D         Letoltes / Osszes letoltese",
    [STR_HELP_PL_IMPORT] = "p           YouTube lista importalasa",
    [STR_HELP_PL_SYNC] = "u           YouTube lista szinkronizalasa",
    [STR_HELP_PL_DELETE] = "x           Lista torlese",
    [STR_HELP_OTHER] = "EGYEB:",
    [STR_HELP_SETTINGS] = "S           Beallitasok",
    [STR_HELP_ABOUT] = "i           Nevjegy",
    [STR_HELP_HELP] = "h,?         Sugo megjelenitese",
    [STR_HELP_QUIT] = "q           Kilepes",
    [STR_HELP_CONTINUE] = " Nyomj egy billentyut a folytatashoz... ",

    // Input prompts
    [STR_PROMPT_JUMP] = "Ugras ide (pp:mm): ",
    [STR_PROMPT_SEARCH] = "Kereses: ",
    [STR_PROMPT_NEW_PLAYLIST] = "Uj lista neve: ",
    [STR_PROMPT_DELETE_FMT] = "'%s' torlese? (i/n): ",
    [STR_PROMPT_RENAME_FMT] = "'%s' atnevezese erre: ",
    [STR_PROMPT_YT_URL] = "YouTube lista URL: ",
    [STR_PROMPT_PL_NAME] = "Lista neve: ",
    [STR_PROMPT_MODE] = "Mod: (s)treameles vagy (l)etoltes: ",
    [STR_PROMPT_SEEK_STEP] = "Tekeres lepese (1-300 mp): ",
    [STR_PROMPT_YT_SYNC] = "YouTube lista URL a szinkronhoz: ",

    // Status messages
    [STR_STATUS_WELCOME] = "Nyomd meg a /-t a kereseskez, d letoltes, f listak, h sugo.",
    [STR_STATUS_RESUMING_PL_FMT] = "Folytatas: %s, %d. szam",
    [STR_STATUS_RESUMING_SEARCH_FMT] = "Folytatas: kereses '%s', %d. szam",
    [STR_STATUS_AUTOPLAY_FMT] = "Kovetkezo: %s",
    [STR_STATUS_FINISHED] = "Lejatszas befejezodott",
    [STR_STATUS_EDIT_CANCELLED] = "Szerkesztes megszakitva",
    [STR_STATUS_PATH_SAVED] = "Letoltesi mappa mentve",
    [STR_STATUS_PAUSED] = "Szuneteltetve",
    [STR_STATUS_PLAYING] = "Lejatszas",
    [STR_STATUS_NEXT] = "Kovetkezo szam",
    [STR_STATUS_PREV] = "Elozo szam",
    [STR_STATUS_SHUFFLE_FMT] = "Keveres: %s",
    [STR_STATUS_SEEK_BACK_FMT] = "<< -%dmp",
    [STR_STATUS_SEEK_FWD_FMT] = ">> +%dmp",
    [STR_STATUS_JUMP_FMT] = "Ugras: %d:%02d",
    [STR_STATUS_INVALID_TIME] = "Ervenytelen idoformatum",
    [STR_STATUS_CANCELLED] = "Megszakitva",
    [STR_STATUS_PLAYING_FMT] = "Lejatszas: %s",
    [STR_STATUS_SEARCHING_FMT] = "Kereses: %s ...",
    [STR_STATUS_SEARCH_ERROR] = "Keresesi hiba!",
    [STR_STATUS_NO_RESULTS_FMT] = "Nincs talalat: %s",
    [STR_STATUS_FOUND_FMT] = "%d talalat erre: %s",
    [STR_STATUS_SEARCH_CANCELLED] = "Kereses megszakitva",
    [STR_STATUS_STOPPED] = "Lejatszas leallitva",
    [STR_STATUS_SELECT_PLAYLIST] = "Valassz listat",
    [STR_STATUS_NO_SONG] = "Nincs kivalasztott dal",
    [STR_STATUS_CREATED_FMT] = "Lista letrehozva: %s",
    [STR_STATUS_EXISTS_FMT] = "A lista mar letezik: %s",
    [STR_STATUS_CREATE_FAILED] = "Nem sikerult letrehozni a listat",
    [STR_STATUS_EDITING_PATH] = "Letoltesi mappa szerkesztese...",
    [STR_STATUS_QUEUED_FMT] = "Sorba allitva: %s",
    [STR_STATUS_ALREADY_DL] = "Mar letoltve vagy sorban",
    [STR_STATUS_QUEUE_FAILED] = "Nem sikerult sorba allitani",
    [STR_STATUS_OPENED_FMT] = "Megnyitva: %s",
    [STR_STATUS_DELETED] = "Lista torolve",
    [STR_STATUS_DELETE_FAILED] = "Nem sikerult torolni",
    [STR_STATUS_RENAMED_FMT] = "Atnevezve: '%s'",
    [STR_STATUS_RENAME_FAILED] = "Nem sikerult atnevezni",
    [STR_STATUS_RENAME_EXISTS_FMT] = "A '%s' nevu lista mar letezik",
    [STR_STATUS_INVALID_URL] = "Ervenytelen URL",
    [STR_STATUS_VALIDATING] = "URL ellenorzese...",
    [STR_STATUS_FETCH_FAILED] = "Nem sikerult lekerni a listat",
    [STR_STATUS_INVALID_MODE] = "Ervenytelen mod. Valaszd az 's' vagy 'l' opicot",
    [STR_STATUS_QUEUED_BULK_FMT] = "%d dal sorba allitva (%d mar letoltve)",
    [STR_STATUS_ALL_DL_FMT] = "Mind a %d dal mar le van toltve",
    [STR_STATUS_PL_EMPTY] = "A lista ures",
    [STR_STATUS_REMOVED_FMT] = "Eltavolitva: %s",
    [STR_STATUS_REMOVE_FAILED] = "Nem sikerult eltavolitani",
    [STR_STATUS_QUEUED_SONGS_FMT] = "%d dal sorba allitva",
    [STR_STATUS_ALL_QUEUED] = "Minden dal mar sorban vagy letoltve",
    [STR_STATUS_SYNCING] = "Lista szinkronizalasa...",
    [STR_STATUS_SYNC_ADDED_FMT] = "%d uj dal hozzaadva",
    [STR_STATUS_SYNC_UPTODATE] = "A lista naprakesz",
    [STR_STATUS_SYNC_CANCELLED] = "Szinkron megszakitva",
    [STR_STATUS_NOT_YT] = "Nem YouTube lista",
    [STR_STATUS_INVALID_YT_URL] = "Ervenytelen YouTube lista URL",
    [STR_STATUS_ADDED_TO_FMT] = "Hozzaadva: %s",
    [STR_STATUS_ADD_FAILED] = "Mar a listaban van vagy hiba tortent",
    [STR_STATUS_CREATED_ADDED_FMT] = "'%s' letrehozva es dal hozzaadva",
    [STR_STATUS_SEEK_SET_FMT] = "Tekeres lepese: %d masodperc",
    [STR_STATUS_SEEK_INVALID] = "Ervenytelen ertek (1-300)",
    [STR_STATUS_REMEMBER_FMT] = "Munkamenet megjegyzese: %s",

    // Dependency errors
    [STR_DEP_YTDLP] = "yt-dlp nem talalhato! A kovetkezo inditaskor automatikusan letoltodik.",
    [STR_DEP_MPV] = "mpv nem talalhato! Telepitsd: apt install mpv",

    // Duration placeholder
    [STR_DURATION_UNKNOWN] = "--:--",

    // Progress callbacks
    [STR_FETCHING_INFO] = "Lista informaciok lekerese...",
    [STR_FETCHING_SONGS] = "Dalok lekerese...",
    [STR_FETCHED_FMT] = "%d dal lekerdezve...",
    [STR_FETCH_COMPLETE_FMT] = "Kesz! %d dal lekerdezve",
};

static const char **all_strings[LANG_COUNT] = {
    [LANG_EN] = strings_en,
    [LANG_HU] = strings_hu,
};

const char *tr(StringId id) {
    if (id < 0 || id >= STR_COUNT) return "???";
    const char *s = all_strings[current_language][id];
    if (s) return s;
    // Fall back to English
    s = strings_en[id];
    if (s) return s;
    return "???";
}

const char *lang_name(Language lang) {
    switch (lang) {
        case LANG_EN: return "English";
        case LANG_HU: return "Magyar";
        default: return "English";
    }
}

Language lang_from_name(const char *name) {
    if (!name) return LANG_EN;
    if (strcmp(name, "hu") == 0 || strcmp(name, "Hungarian") == 0 || strcmp(name, "Magyar") == 0) return LANG_HU;
    return LANG_EN;
}
