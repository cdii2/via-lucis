# Via Lucis — Songbook

Christian's practice ladder. Two lessons in, knows note names; wait mode carries the
rest. **North star: Richard's Villa (Link's Awakening).**

## Legal split (LOCKED)

- `songs/pd/` — public-domain pieces, **committed** to the repo (ships with the project).
- `songs/local/` — copyrighted game/film arrangements, **gitignored**, personal use
  only, never published. Lives on Christian's machine + the ESP32.

## Sources

- **ninsheetmusic.org** — Nintendo/game sheet music with direct MIDI downloads (free).
- **vgmusic.com** — classic game MIDI archive (free, quality varies; check hand split).
- **MuseScore.com** — biggest catalog (NieR/Kajiura/Touhou all present). Downloads need
  Christian's account → those go in the **Needs Christian** queue.
- Files with merged hands: open in MuseScore desktop, split, re-export.

## Tier 0 — week one (slow, sparse, melody + simple left hand)

| Piece | From | Source guess |
|---|---|---|
| Song of Storms | Zelda OoT | ninsheetmusic / vgmusic |
| Midna's Lament | Zelda TP | ninsheetmusic |
| Ballad of the Wind Fish | Link's Awakening | ninsheetmusic |
| Mipha's Theme | Zelda BotW | ninsheetmusic |
| Grandma | FF Crystal Chronicles | MuseScore |
| PD starter pack (ships in repo): Ode to Joy, Amazing Grace, Minuet in G, simple Vivaldi Largo arrangement | — | we arrange/commit |

## Tier 1 — first months

Title Theme (Zelda OoT) · Hateno Village (BotW) · Field – Day (BotW) · Sadness and
Sorrow (Naruto) · The First Town (SAO, Kajiura) · Friendly Feelings (SAO, Kajiura) ·
Yui (SAO, Kajiura) · Selection Menu (Zelda TP) · Lake Hylia (Zelda TP) · All-Star Rest
Area (Smash Bros. Melee) · Ballad of the Goddess (Zelda SS)

## Tier 2 — intermediate (the heart of the list)

**Richard's Villa (Link's Awakening) ★ NORTH STAR** — swing feel, trickier than it
sounds, reachable.

NieR Gestalt/Replicant set: Yonah · Snow in Summer · Song of the Ancients / Hollow
Dreams · Kainé (Salvation) · Emil (Sacrifice) · Dispossession · Hills of Radiant
Winds · Peaceful Sleep / 穏ヤカナ眠リ (same track, Japanese title).

NieR: Automata set: Weight of the World · Vague Hope / Cold Rain · Voice of no Return
(Akisai piano) · Kuroi Uta (Drakengard 3).

Others: Deference for Darkness (Halo 3 ODST) · Character Making (FF Tactics) · Ul'dah
Night / Sultana Dreaming (FFXIV) · Niflheim Theme (Ragnarok Online) · He's a Pirate /
Jack Sparrow (Zimmer) · Last Stardust (Aimer, Fate/UBW) · The Sunleth Waterscape
(FFXIII) · Lightning's Theme (FFXIII, Hamauzu) · Vivaldi Variation (Florian Christl —
copyrighted arrangement; underlying Vivaldi is PD) · Shurelia (Ar tonelico — PDF at
`C:\Users\omega\Downloads\shurelia.pdf`; check MuseScore for existing transcription
first, else first OMR test case) · Present (Lloyd/Vaan — exact track unidentified,
ask Christian which album) · Small Village (Hikari/Raouf, Vagabond vibe — identify
exact source) · **David (Angel Studios film): I Will Not Be Afraid (Reprise) + other
soundtrack songs** (recent film — MuseScore transcriptions may be sparse; check
periodically).

## Tier 3 — aspiration

Clair de Lune (Debussy — **public domain**, ships in repo) · A Beautiful Song
(NieR: Automata) · Septette for the Dead Princess (Touhou — listed twice by Christian;
we know where his heart is).

## Status

Updated 2026-07-07 (W5 agent). All files validated against firmware limits
(SMF 0/1, ticks-per-quarter division, has notes, <256KB). "Hands" legend:
**OK** = named Left/Right tracks (auto hand assignment) · **auto-2** = two
unnamed note tracks (firmware assigns first=Right, second=Left) · **multi** =
ensemble file; pick the melody/bass tracks in the web UI, or MuseScore-split
for a proper two-hand version.

### songs/pd/ (committed, CC0 arrangements — built by songs/pd/generate.py)

| File | Work | Tier | Hands | Valid |
|---|---|---|---|---|
| ode-to-joy.mid | Beethoven, Hymn to Joy (C major, 16 bars) | 0 | OK | PASS |
| amazing-grace.mid | New Britain hymn tune (G major, 3/4) | 0 | OK | PASS |
| minuet-in-g.mid | Petzold BWV Anh. 114 (full 32 bars) | 0 | OK | PASS |
| vivaldi-largo.mid | Winter RV 297 mvt 2, Eb→C (18 bars, Mutopia urtext) | 0 | OK | PASS |
| clair-de-lune.mid | Debussy, opening bars 1–9, Db→C (Mutopia urtext) | 3→starter | OK | PASS |

### songs/local/ (gitignored, personal use — vgmusic.com, downloaded 2026-07-07)

| File | Source (vgmusic.com) | Tier | Hands | Valid |
|---|---|---|---|---|
| song-of-storms.mid | …/nintendo/n64/windmill_hut2.mid | 0 | multi (6 trk) | PASS |
| ballad-of-the-wind-fish.mid | …/nintendo/gameboy/Zgbreve.mid | 0 | auto-2 | PASS |
| midnas-lament.mid | …/nintendo/wii/tppiano.mid | 0 | multi (5 trk, piano seq) | PASS |
| title-theme-oot.mid | …/nintendo/n64/Ocarina_of_Time_-_Title_Theme.mid | 1 | multi (6 trk) | PASS |
| lake-hylia.mid | …/nintendo/wii/TP_LakeHylia.mid | 1 | multi (6 trk) | PASS |
| all-star-rest-area.mid | …/nintendo/gamecube/SSBMASIN.mid | 1 | multi (6 trk, harp+piano) | PASS |
| ballad-of-the-goddess.mid | …/nintendo/wii/Ballad_Of_The_Goddess.mid | 1 | multi (9 trk) | PASS |

Full vgmusic path prefix: `https://www.vgmusic.com/music/console/`.

### Needs Christian — NinSheetMusic (site Cloudflare-blocks automation)

ninsheetmusic.org hard-blocked the agent's browser (Turnstile loop, then
ERR_BLOCKED_BY_RESPONSE), so its clean two-hand piano MIDIs need a human
click. Direct MIDI links (one click each, save into `songs/local/`):

- Midna's Lament — https://www.ninsheetmusic.org/download/mid/206
  (replaces the vgmusic sequencer version above)
- Mipha's Theme — https://www.ninsheetmusic.org/download/mid/3551 (Tier 0, not on vgmusic)
- Hateno Village (Day) — https://www.ninsheetmusic.org/download/mid/3831 (Tier 1, not on vgmusic)
- Field (Day) BotW, Selection Menu (TP) + nicer piano arrangements of Song of
  Storms / Wind Fish / OoT Title / Lake Hylia / Ballad of the Goddess — browse
  https://www.ninsheetmusic.org/browse/series/TheLegendofZelda (MID button per sheet)

### Needs Christian — MuseScore (account downloads)

- Grandma (FF Crystal Chronicles, Tier 0) — https://musescore.com/sheetmusic?text=grandma%20crystal%20chronicles
- Sadness and Sorrow (Naruto, Tier 1) — https://musescore.com/sheetmusic?text=sadness%20and%20sorrow%20naruto
- The First Town (SAO, Tier 1) — https://musescore.com/sheetmusic?text=the%20first%20town%20sword%20art%20online
- Friendly Feelings (SAO, Tier 1) — https://musescore.com/sheetmusic?text=friendly%20feelings%20sword%20art%20online
- Yui (SAO, Tier 1) — https://musescore.com/sheetmusic?text=yui%20sword%20art%20online

(Anime OSTs are not on either authorized site — game-music archives only.)
