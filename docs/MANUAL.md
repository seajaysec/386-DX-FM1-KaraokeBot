# 386DX manual

386DX turns the M-VAVE FM-1 into a singing robot with a band. It sings karaoke songs you bring in
from a computer and lines you type on the FM-1 itself, to notes you play on its keys. Keys, Bass
and Drums play along, and sixteen effects punch in under your fingers.

## The words

- A **song** is its **lines**, its **rows** and its backing.
- A **line** is a lyric and its tune: one note for each syllable, or more than one when a vowel is
  held across notes.
- A **row** says "sing line 3 here". The song sings its rows in order. Two rows can sing the same
  line: a chorus is written once and sung as often as you like. A line's number is dark on the
  Song page when more than one row sings it.
- The **backing** is three tracks, Keys, Bass and Drums. Each bar plays one of sixteen
  **patterns**, a 16-step grid for the three tracks.
- A **track** is one of Voice, Keys, Bass and Drums. ALGORITHM picks the track on every page; the
  keys play it.

## The buttons

A button goes to one place. HOME's place has two views of one idea: the robot singing, and the
song it sings.

| Control | What it does |
|---|---|
| HOME | **Sing**: the robot and the words being sung. Again: **Song**, the rows of the song. Held, it is shift (below) |
| SEQ | **Steps**: the selected backing track's pattern for a bar. The white keys are its 16 steps |
| EDIT | **Sound**: the selected track's sound |
| ENV | **Envelope**: the sound in time: attack, release, filter; the Voice's glide and vibrato |
| FX | **Effects**: reverb, tape echo, drive, squeeze, pump, filter. The white keys punch in effects |
| LFO | **Mixer**: each track's level, reverb and echo |
| GLO | **Setup**: tempo, length, meter; then Status. Its list loads, saves and starts songs |
| ARP | **Change**: the bar's pattern shifted, pitched, or woven with another |
| SELECT | The place's next page. In a list or a grid, it moves |
| ALGORITHM | The track: Voice, Keys, Bass or Drums. On Change's *WEAVE IN*, the pattern |
| PRESETS | The place's thing: a sound on Sound, Envelope and Sing, a row's line on Song, a bar's pattern on Steps and Change |
| KNOB 1–4 | The four values on the strip at the bottom of the screen |
| PLAY | Play, or stop. On Song it plays from the row under the cursor, on Steps from the bar |
| REC | Write what you play: notes into a line (Song), steps into a pattern (Steps), or over the song while it plays |
| SAVE | Save the song. Held, with white key 1–8: save it to that slot. A song with no slot opens Setup's SAVE TO |
| SEL | A list of everything else in this place. In a question, yes |
| OCT− / OCT+ | The keys an octave down or up. While writing notes: back, and a rest |

HOME held is shift:

| Combination | What it does |
|---|---|
| HOME + REC | Undo (one step) |
| HOME + PLAY | Redo |
| HOME + SELECT | Tempo |
| HOME + a white key | A punch-in effect, held |
| HOME + black key 1–4 | Mute or unmute Voice, Keys, Bass or Drums |
| Any button, held alone | A card says what it does here |

## Getting started: write a line and hear it sung

These are the steps `tests/scenarios/write.sim` presses and checks.

1. Press **GLO** (Setup), then **SEL**. Turn **SELECT** to *NEW SONG* and press **SEL**. The song you
   had is kept, and the new one opens on Song.
2. Press **SEL**, and **SEL** again for *NEW LINE*. The keys are now letters, shown on the
   screen as they lie on the keyboard. Type *hello robot friend*. **OCT−** deletes, **SELECT**
   moves the cursor. Press **SEL** to keep it: it is saved in the song's slot at once.
3. Press **REC**. The header says how many syllables the robot hears (*5 SYLLABLES*). Each key you press is the next syllable's note, and you hear the syllable sung
   at that pitch. **KNOB 4** sets the next note's length. **OCT+** leaves a rest, **OCT−** takes
   the last note back. Hold a key and press another to carry the same vowel onto a second note.
   Play five notes; *LAST SYLLABLE* says the line is complete. Press **REC** to finish.
4. Press **PLAY**. The robot sings the line over the beat. Press **PLAY** to stop.
5. Turn **SELECT** down to the **+** row and press **SEL**, **SEL** for a new row, then **HOME**
   to leave the words empty. Turn **PRESETS** to line 1. Now two rows sing the same line, and its
   number turns dark.
6. Press **HOME** (back to Sing) and **PLAY**. Watch the robot: its mouth opens with the sound, its eyes ride
   up with the pitch, and the word being sung lights up under it.
7. Press **SAVE**. The song has no slot yet, so Setup's list opens at *SAVE TO*, on the first empty
   slot. **SELECT** picks the slot, **SEL** saves. From then on SAVE saves there, and the song
   saves itself a few seconds after you stop touching anything.

## The lights

The keys light to say what they do here. Typing: a few keys to find your place by, and the last one
pressed blinking. Voice on Sing or Song: the keys the line sings, the next one blinking. Steps: the
steps that play, the one playing now and the one you hold blinking. Mixer: the tracks that play. Any
other page: the white keys. In a list, a question or typing, only the buttons that work there light.

## The pages

### Sing (HOME)

The robot. While the song plays, the words being sung light up under it; stopped, it shows the
song's name. With Voice selected the keys make it sing pitches, an "ah" at each key, without
words: the words are the song's, sung from Song or when it plays. The header names Song beside
Sing: HOME again goes there.

Its face is the voice:

| The robot | Comes from |
|---|---|
| The mouth opens | How loud the voice is now |
| The mouth widens | How bright it sounds |
| A little hop | Each syllable as it starts |
| Sitting higher or lower | The pitch, against the song's middle |
| ^ ^ eyes, or calm – – eyes | A high note, or a low one, five semitones or more from the middle |
| The outline going wavy | Vibrato: further the deeper it is |
| A bigger or smaller head | BODY |
| Glitches | Punch-in effects held |

The knobs: BODY and VIBRATO for the voice, FILTER and PUMP for the whole mix. PRESETS changes the
selected track's sound.

### Song (HOME, again)

One row a line of the screen: where it starts (bar.beat), the line's number, its words. A coral dot
means the words have no tune yet. The cursor is the coral ring; the row singing now is tinted teal.

- **SELECT** moves the cursor; **PRESETS** chooses the row's line (any line, so rows can share one).
- **KNOB 1–2**: the row's bar and beat. **KNOB 3**: its line. **KNOB 4**: the length of the next
  note you write.
- **The keys** sing the cursor row's line, a syllable a press, at the key's pitch, and hold its
  vowel while the key is down.
- **REC** writes notes (see Getting started). A line that already has all its notes is written
  again from the top.
- **HOME** goes back to Sing.
- **SEL**: EDIT WORDS, NEW LINE, COPY AS NEW LINE (this row gets its own copy, to change without
  changing the other rows), MOVE, CLEAR NOTES, DELETE ROW.
- **MOVE** carries the row's line: turn **SELECT** and it passes the lines above or below (the
  places stay, the lines change places), **SEL** drops it there, **HOME** puts it back.

Lines and rows change only while stopped.

### Steps (SEQ)

The selected track's pattern for the bar under the cursor. The header says which pattern the bar
plays and how many bars play it: a change to the pattern is heard in all of them.

- **The white keys are the 16 steps.** Each one lights when its step plays something.
- **Drums**: every drum sound is a row; a white key switches the selected sound's step on and off,
  the black keys pick the sound.
- **Keys and Bass**: a white key writes the remembered notes on its step, or takes the step's notes
  away. Hold **SEQ** and play to change the remembered notes: a chord of up to four on Keys, one
  note on Bass. The footer shows them. **REC** writes at the cursor instead: play a key, or a chord,
  and the cursor moves on by the length. **OCT+** writes a rest, **OCT−** steps back.
- **Hold a white key** to work on that step: **KNOB 1** its velocity, **KNOB 2** its length,
  **KNOB 3** its note (a chord moves with it). Holding a step, press a **later white key** and its
  note lasts until there; on **Drums**, press a **black key** to put that drum on the step, or take
  it off.
- **OCT− / OCT+**: the bar before or after.
- **KNOB 1–4**: bar, step, velocity, length (of the steps you write next). **PRESETS**: the pattern
  this bar plays.
- **SEL**: CLEAR TRACK, CLEAR PATTERN.

While the song plays, the keys play the track, and **REC** records what you play into the bar
playing, on the nearest step.

### Sound (EDIT)

- **Voice**: BODY, BREATH, EDGE, OCTAVE. BODY, BREATH and EDGE change how the words are pronounced:
  sung syllables are made again with them, a moment after you turn.
- **Keys and Bass**: each has its own sounds, PRESETS stepping through them (the page shows eight
  at a time, the one playing ringed). Bass: deep sub, acid squelch, reese, FM growl, pluck bass,
  wobble (its filter in eighth notes), boom, fat saw and seven more. The knobs are the sound's
  engine's own four. On Bass the keys play two octaves down, F1 to G3.
- **Drums**: PRESETS picks a kit (Studio, Tight, Boom, Dusty, Bright, Crunch). The knobs are TUNE,
  DRIVE, TONE.
- **SEL**: SOUND AS PRESET (every knob back to the preset's).

### Envelope (ENV)

- **Voice**: GLIDE, VIBRATO, RATE.
- **Keys and Bass**: ATTACK, RELEASE, CUTOFF, RESO.
- **Drums**: FUNK (each hit pushed a little late), ATTACK, RANDOM (each hit a little different),
  LENGTH.

The screen draws the shape: the rise, the hold, the fall.

### Effects (FX)

The knobs: REVERB, TONE, DELAY (the echo's time, in divisions of the beat), REPEATS (its feedback); then DRIVE,
SQUEEZE (the master compressor), PUMP (the mix ducks on each kick), FILTER (low-pass to the left,
high-pass to the right, off in the middle).

The white keys punch in effects while held, on this page or anywhere with HOME held:

On this page the first white key of each row of four and every fourth after it are lit, to find your place
by. Several effects can be held at once; of the ones that replay the last moment (the brrrs, tape off,
reverse, half time) the lowest key wins. The light under the key or keys last pressed blinks after they
are let go.

| Key | Effect | Key | Effect |
|---|---|---|---|
| 1 | lo-pass | 9 | tape off |
| 2 | hi-pass | 10 | reverse |
| 3 | phone | 11 | chop 8 |
| 4 | crush | 12 | chop 16 |
| 5 | brrr 8 | 13 | ring mod |
| 6 | brrr 16 | 14 | half time |
| 7 | brrr 32 | 15 | mute |
| 8 | brrr 64 | 16 | lo-fi |

The brrrs, tape off, reverse and half time replay the last quarter second; with two held, the
lower key wins. The others work on top of them.

### Mixer (LFO)

Each track's level, then (SELECT) its reverb and its echo, KNOB 1–4 for Voice, Keys, Bass,
Drums. Black keys 1–4 mute and unmute.

### Setup (GLO)

TEMPO, BARS (the song's length), METER (4/4 or 3/4). The bottom line says which 386DX build the
FM-1 runs.

**SEL** is where songs come and go. *LOAD*, *SAVE TO* and *NEW SONG* each open the eight slots, every
one with its song's name (or *empty*, and a dot on the song you are in): **SELECT** moves, **SEL** does
it, **HOME** goes back. *NEW SONG* starts on the first empty slot and asks (*REPLACE S3?*) only for one
that holds a song; the song you had is kept first. *RENAME SONG* edits the name. SAVE with no slot
yet opens *SAVE TO* the same way.

### Change (ARP)

What happens to the bar's pattern, for the selected track. **KNOB 4** and **PRESETS** pick the bar
and the pattern it plays, as on Steps.

- **SHIFT** (KNOB 1) turns the track's steps later or earlier around the bar.
- **PITCH** (KNOB 2) moves every note of the track's pattern up or down, a semitone a detent. With
  Voice selected it moves the line under the Song cursor instead (stopped).
- **WEAVE** (KNOB 3) picks another pattern; **SEL**, *WEAVE IN* adds its steps to this one: its
  drum hits, its Keys notes into the chords (up to four), its Bass notes where this one rests.
  Nothing is taken away; HOME + REC undoes it.
- **SEL** also has NEW PATTERN FOR THIS BAR (a copy of its own) and EVERY BAR PLAYS THIS.

Shift, pitch and weave work while the song plays, so you hear them land.

## The companion page

Open `web/index.html` from a local web server in Chrome or Edge, with the FM-1 plugged in.
`tools/build.py` puts the firmware beside the page; `tools/make_companion.py` makes the folder to
publish.

- **Songs on the FM-1** (shown once the page is connected to an FM-1 running 386DX): the eight slots, each with what it holds and a menu of what should happen
  to it: one of our songs (thirteen public-domain standards) or *Import a file…* (a karaoke .kar or
  MIDI .mid file with its words as timed lyrics) put there, *Download it*, or *Delete it*. Each
  change waits, highlighted, until you press *Sync*, which does them all: a song is found in its
  file, every syllable checked with the FM-1's own voice, and saved in its slot. Keep the FM-1 on
  and plugged in while it syncs; when it is done it restarts by itself, with the song saved last.
  A song you were working on is saved before another replaces it.
- **Connect** reads which 386DX build the FM-1 runs and compares it with the one this page
  carries. The button under it says what it will do: *Install 386DX* on an FM-1 with other firmware,
  *Update to* the page's build when they differ, *Install again* when they match, *Finish the
  update* when an update was cut off. It works without connecting first too. If the FM-1 doesn't
  answer under its usual name (a Mac can show it under another device's name), the page lists every
  MIDI device connected: pick the FM-1 and the page remembers it.
- The build is the commit it was made from, with *+* and a fingerprint when it had uncommitted
  changes. Setup's Status page on the FM-1 shows it too.

## The first time

The first time 386DX starts on an FM-1, its eight song slots are cleared (nothing another firmware
left there is read as a song) and S1 gets Greensleeves, which plays at once. It is a song like any
other: change it, save over it. After that the FM-1 always starts with the song saved last. If it
ever fails to start, the next start skips loading a song, so a damaged song can't stop it twice.

## How much fits

A song holds 128 bars, 64 lines, 96 rows, 512 sung notes and 16 patterns. The FM-1 keeps 8 songs.
Words are up to 63 letters a line; a line has at most 64 syllables. A digit in a lyric is read
one digit at a time: spell numbers out.
