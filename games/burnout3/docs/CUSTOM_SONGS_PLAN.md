# Custom songs in EA Trax

Status (2026-10-19): **done** - engine 1cc92de (music.h), port
custom_songs.cpp. The design below as built, but for playing: rather than
running the player's states itself, a custom song gets a stand-in stream
(kFakeStream) and the RenderWare Audio calls the player makes for it are
answered by the host, so the player's own code (states, banner, resume
point, next song) runs unchanged. Checked: the menu lists the songs and
keeps their choices (custom_songs/settings.ini; the card's 44 bytes as
ever), a song previewed plays to its end and the player goes on, 10 songs
(54 in all) through the race on the random order's port; every scripted
run as recorded with no songs.

Goal: the player drops MP3s into a `custom_songs` folder beside the game. Each one
appears in the EA TRAX menu with the game's own songs, with the same ALL / RACE
ONLY / MENU ONLY / OFF setting, and plays in the rotation (random or sequential,
L2 skipping it, its banner at a race's start). The name comes from the file name,
cut to what the menu has room for.

## How the game's EA Trax works (2026-10-19)

- **The songs.** 44 songs, 22 in each of `TRACKS/_EATRAX0.RWS` and
  `_EATRAX1.RWS` (RenderWare Audio streams, ~157 MB each, one segment a song).
  The player builds the path from `tracks\` `_EATrax` `0|1` `.rws` (0x4CEA60-
  0x4CEA90): a song below 22 is in file 0, the rest in file 1.
- **The table** at 0x4A5600, 24 bytes a song: `{index, ?, title, album, artist,
  setting}`, the three names string IDs into GLOBALUS's table (477-596, then
  3276-3287 for the last four), `setting` bits 0-3 (15 for all at first:
  0x3FBAA0 clears bits 0-3 and ORs the menu's choice).
- **The descriptor** at 0x4A5A20: `+0` current, `+4` count (44), `+0xC` 64
  bytes of history, `+0x4C` the table's address (0x4A5600), `+0x54` music on.
  Everything after start-up reaches the songs through it, or through the
  player's copy of it (0x3FCD20: `+0x6C` count, `+0xB4` table).
- **The player** (the object at 0x01E7A888, code 0x3F8040-0x3FD1F0):
  - 0x3FBC20 plays song n (or stops it, n = -1): file and segment from n.
  - 0x3FB7C0: whether the current song may play here (its setting bits
    against menu / race).
  - 0x3FB8B0: random order and the previous song. **Its order is a byte array
    inside the player object (+0x0D up to +0x40): room for 51 songs.**
  - 0x3FBB00 / 0x3FBAA0: a song's entry, and setting a song's choice (the
    menu).
- **The saved choices.** The options block (0x28B220 applies it) holds one
  byte a song from offset 0 and other audio settings from +0x33: 51 at most,
  on the memory card. 0x3FCC10 also copies 44 of them, a hard 44.

## Design

The game's 44 songs and its save stay exactly as they are. Custom songs are added
after them, at index 44 and up, and everything about them that does not fit the
game's structures is the host's.

1. **Scan** `custom_songs/*.mp3` at start (and again when the EA TRAX menu
   opens: songs added while the game runs appear). Sorted by name. A cap of
   100 custom songs.
2. **Names.** The template is `Artist - Title.mp3`: the artist is what comes
   before the first " - ", the title the rest; without " - " the whole name is
   the title and the artist "Custom". Each is cut to the menu's room (measured
   on the EA TRAX screen; the game's longest title is 40 characters) with
   "..." where cut. Characters outside the game's font are replaced.
3. **The list.** A table of 44 + N entries in free guest memory: the game's 44
   copied, custom entries after with string IDs from a custom range. The
   string lookup is overridden for that range (the host's names). The
   descriptor's count and table address point to it before the player starts.
4. **Past 51 songs.** The random order (0x3FB8B0) and the saved choices
   (0x28B220, 0x3FCC10 and the save's reverse) are overridden: the order in an
   array of the host's, custom songs' choices in `custom_songs/settings.ini`.
   The game's 44 still read and write the memory card as before.
5. **Playing.** 0x3FBC20 for a custom song starts no RWS stream: the host
   decodes the MP3 (minimp3, public domain, in engine/third_party) and mixes it
   into the SPU2's output (AudioFrames: the game's clock, so it pauses and
   keeps time with the game), at the game's music volume and fades. The
   player's polling of its stream sees the song playing until the MP3 ends,
   then ended, so the game moves on as after its own songs; L2 skips it.
6. **README:** the folder, the name template with an example, the cap and
   the length limit.

## Phases

1. Host: scan, names, MP3 decoding and mixing (a test switch plays one).
2. Game: the extended list in the EA TRAX menu (names shown, choices kept).
3. Game: playing a custom song in the rotation (start, end, skip, pause,
   volume, banner).
4. Settings file, release folder, README; regressions (the game's songs and
   save unchanged with an empty folder).

## Checks

- An empty `custom_songs`: every scripted run's pictures as recorded, the save
  as before.
- With songs: the menu lists them; choosing OFF / RACE ONLY works and is kept;
  a race plays them (audio checked with --audio-wav and audiocheck.py).
