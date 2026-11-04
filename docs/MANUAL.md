# 386DX manual

386DX turns the M-VAVE FM-1 into a singing robot with a band. It sings karaoke songs you bring in
from a computer and lines you type on the FM-1 itself, to notes you play on its keys. Keys, Bass
and Drums play along, and sixteen effects punch in under your fingers.

## The words

- A **track** is one of Voice, Keys, Bass and Drums. ALGORITHM picks the track on every page; the
  keys play it. A stripe under the top bar is always the selected track's colour: coral Voice, blue Keys,
  gold Bass, green Drums.
- Every track is a **chain** of **phrases**. A phrase is one or more bars (most are one to four); an empty
  one is a rest. A track's chain plays round again when it is shorter than the song, so a one-bar beat
  under a 32-bar song plays 32 times. The song is as long as its longest chain.
- Inside a bar are **steps**, sixteen of them (twelve in 3/4). A note starts on a step and lasts whole steps.
  It can be **nudged** up to half a step early or late, in sixths of a step; the beat stays on the grid.
- A bar of Keys, Bass or Drums can be **linked** to others: DUPE makes the same bars play again, and a change
  to one is heard in all of them. Phrase says how many bars a bar is linked to.
- On the Voice a phrase holds **lines**: a lyric and its tune, one note for each syllable, or more than one
  when a vowel is held across notes. A line starts at its own place, a pickup before the bar line included.

## The buttons

A button goes to one place, and pressed again where you are, it stays. A place with layers goes a layer on
with **HOME** held: **HOME + SEQ** goes Song, Phrase, (the Voice's) words, and round; **HOME + FX** and
**HOME + LFO** go to the next bank of knobs. Pressing a button alone never moves you off it, so it can be held
for its help card.

| Control | What it does |
|---|---|
| HOME | **Sing**: the robot and the words being sung. Held, it is shift (below), and stays where you are |
| SEQ | **The sequencer**: **Phrase**, the phrase under the song's cursor, a bar at a time. With HOME: the next layer, **Song** (every track's chain), then back into the phrase, and on the Voice its **words** |
| EDIT | **Sound**: the selected track's sound |
| ENV | **Envelope**: the sound in time: attack, release, filter; the Voice's glide and vibrato |
| FX | **Karaoke**: the karaoke room. The white keys punch in effects, the knobs are the effects' and the Voice's |
| LFO | **Mixer**: each track's level, reverb and echo |
| GLO | **Setup**: the song's tempo, its key (ROOT and SCALE) and its meter. Its list resets every knob |
| ARP | **Arp**: Keys' and Bass' arpeggiator: the notes held, one at a time |
| SELECT | The place's next page of knobs, and round again. On Phrase, the cursor. In a list, it moves (past the end, back to the top) |
| ALGORITHM | The track: Voice, Keys, Bass or Drums, and round again |
| PRESETS | The selected track's sound, on Sound, Envelope and Sing: it changes for good, playing or not, and the list goes round |
| KNOB 1–4 | The four values on the strip at the bottom of the screen, each knob's bar in its own colour everywhere: blue, yellow, green, orange |
| PLAY | Play the song from the top, or stop. On Phrase it plays from the start of the phrase you're writing |
| REC | Record what you play over the song while it plays (stopped: armed, PLAY starts it) |
| SAVE | **The songs**: LOAD, SAVE TO, CLEAR SLOT, RENAME SONG. Held: save the song where it lives. Held with white key 1–8: save it to that slot, where it lives from then on |
| SEL | A list of everything else in this place. In a question, yes |
| OCT− / OCT+ | The selected track an octave down or up, on every page: the Voice through its registers Profondo, Baritone, Tenor, Alto and Soprano; all of Keys or Bass, its song and its keys; Drums' kit re-pitched |

HOME held is shift:

| Combination | What it does |
|---|---|
| HOME + REC | Undo (one step) |
| HOME + PLAY | Redo |
| HOME + SELECT | Tempo |
| HOME + black key 1–4 | Mute or unmute Voice, Keys, Bass or Drums |
| HOME + a page's button | That place's next layer (SEQ: Song, Phrase, words; FX and LFO: the next bank) |
| Any button, held alone | A card says what the screen is, then the buttons and knobs it doesn't label, then the ones it does |

## Getting started: write a phrase and hear it sung

These are the steps `tests/scenarios/write.sim` presses and checks.

1. Press **SAVE** for the songs, and **SEL** for *LOAD*. Turn **SELECT** to an empty slot (*S2*) and press
   **SEL**. The song you had is kept, and a new one opens in that slot, ready to write: Phrase, the Voice's
   first phrase, the cursor on its first step.
2. **Tap keys.** Each one writes its note at the cursor, a sixteenth long, sings it, and moves the cursor on a
   step. To write a longer note, hold its key and turn **SELECT**: the note stretches with the cursor. Turning
   **SELECT** with no key held moves the cursor, which leaves rests. Several keys at once make a chord (on
   Keys).
3. Hold **HOME** and press **SEQ**: the Voice's words (or **SEL**, **SEL**: *EDIT WORDS*). The keys are now
   letters, shown on the screen as they lie on the keyboard. Type *hello robot friend*. **OCT−** deletes, **SELECT** moves the cursor. Press
   **SEL** to keep it. Its syllables go to the notes in order.
4. Move the cursor back onto a note. The knobs are now that note's: **KNOB 2** its length, **KNOB 3** its
   pitch, **KNOB 4** its nudge. Hold its key and turn **SELECT** to stretch or shrink it from where it starts.
   Tap its key without turning and it is gone, or **SEL**, *DELETE NOTE*.
5. Press **PLAY**. The robot sings the phrase over the beat. Press **PLAY** to stop.
6. Hold **HOME** and press **SEQ** (twice on the Voice: the words, then the song): the song. **SEL**, *ADD*
   puts an empty phrase after the cursor's; *DUPE* the same phrase again (the same line sung twice); *MOVE*
   carries it with **SELECT**. **HOME + SEQ** goes into a phrase.
7. Hold **SAVE** a moment: the song is saved in its slot (*SAVED S2*). SAVE is lit while the song has
   changes not saved yet; switching the FM-1 off loses them, loading another song does not.

## The lights

The keys light to say what they do here. Typing: a few keys to find your place by, and the last one
pressed blinking. Phrase: the keys of the notes at the cursor, steady where one starts, blinking where one
goes on from before; on Drums, the steps the drum shown plays (where the song is playing shows on the screen
only). Voice on Sing or Song: the keys the line sings, the next one blinking. Mixer: the tracks that play.
The karaoke room: the first key of each family of effects. Any other page: the keys that play the song's key
note, home, in every octave (Drums: the keys with a drum), the key played last blinking. In SLAMJAM the keys that
fit the band's chord right now light instead, and change with it, so anything lit sounds right (in the room, the
black keys that do). In a list, a question or typing, only the buttons that work there light.

## The pages

**Changes are heard.** With the song stopped, whatever you change that has a sound is played for a moment: the
sound you pick, a sound or effect knob, a Voice setting or its register, a track's level or sends, the track you
switch to (its own note, the Voice's "ah", Drums' kick), and in Setup the new key's chord when ROOT or SCALE
turns. While the song plays, the song is what you hear.

### Sing (HOME)

Mister Wiggles, on the stage (below). While the song plays, the words being sung light up in the bar he stands
on, with the song's progress under them; stopped, the bar shows the knobs as on every page. With Voice selected the keys make it sing pitches, an "ah" at each key, without
words: the words are the song's, sung on Song and Phrase or when it plays.

Its face is the voice:

| The robot | Comes from |
|---|---|
| The mouth opens | How loud the voice is now |
| The mouth widens | How bright it sounds |
| A little hop | Each syllable as it starts |
| Sitting higher or lower | The pitch, against the song's middle |
| ^ ^ eyes, or calm – – eyes | A high note, or a low one, five semitones or more from the middle |
| The outline going wavy | Vibrato: further the deeper it is |
| Glitches | Punch-in effects held |

The knobs: BODY and VIBRATO for the voice, FILTER and PUMP for the whole mix. PRESETS changes the
selected track's sound.

### Song (HOME + SEQ, from the phrase)

At the top, the four tracks' chains across the song, a cell a bar: a gap where a phrase starts, a pale cell for
a bar of rest, a faint one where a chain plays round again. The cursor's phrase is ringed; while the song plays a
teal line runs across. Under them, the selected track's phrases, one a row: its bars, and on Keys, Bass and Drums
a block for each bar (pale a rest, marked when it is linked to others); on the Voice its first line's words.

- **SELECT** or **KNOB 1** moves the cursor; **ALGORITHM** picks the track. **KNOB 4**: the phrase's LENGTH in
  bars (bars added at its end are rests; bars taken away go with what was in them).
- **HOME + SEQ** goes into the phrase.
- **SEL**: ADD (an empty phrase after this one, as long), INSERT (before it), DUPE (the same phrase again after
  it: the same bars, linked; the same lines), DELETE (after a question), MOVE, EDIT (into it), BUFF OUT and
  BUFF IN.
- **MOVE** carries the phrase: turn **SELECT** and it passes its neighbours, **SEL** drops it there, **HOME**
  puts it back.
- **BUFF OUT** keeps the phrase; **BUFF IN** puts a copy of its own after the cursor's, on the same track: new
  bars, new lines, to change without changing the first. On Keys, Bass and Drums BUFF IN works in another
  song too.

Phrases change only while stopped. The song is as long as its longest chain, and grows or shrinks with it; a
chain shorter than the song plays round.

### Phrase (SEQ, or HOME + SEQ from Song)

A bar of the phrase under the song's cursor, for the selected track: the phrase's bars are tabs at the top, this
one dark. Where the song is playing shows as a teal column. Every note you write or change is heard as it lands,
for exactly its length; so is a note the cursor moves onto.

**Voice, Keys and Bass: the cursor.** A coral column is the cursor; **SELECT** moves it through the whole phrase,
bar to bar (turned fast, a beat at a time). Stopped, every key is a note in the song's scale:

- **Tap a key**: its note at the cursor, a sixteenth, and the cursor moves on a step. Several keys at once: a
  chord (Keys up to four; Bass keeps one note a step, the newest).
- **Hold a key and turn SELECT**: the note stretches with the cursor; turning back shrinks it. On the Voice a
  longer note writes over the notes it reaches (their syllables go to the notes after). A key on a note already
  there grabs that note instead: put the cursor anywhere on it, hold its key, turn.
- **Tap the key of a note at the cursor** without turning: that note is gone. **SEL**, *DELETE NOTE* does it too
  (the whole note, a chord included).
- **The knobs** are the note at the cursor's: **KNOB 1** velocity (Keys, Bass; the Voice has none), **KNOB 2**
  length, **KNOB 3** pitch (a chord moves together), **KNOB 4** nudge, a sixth of a step early or late, up to
  three. Off a note, **KNOB 1** goes bar to bar.
- On the Voice the notes take the line's syllables in order; a note written into an empty place starts a new
  line. Under the notes the line's words show, the word the cursor is on lit, with **syl 3/11** in the tag: the
  syllable at the cursor, of the line's. Words no note sings yet are in coral. A note past the last syllable holds
  the vowel before it. The Voice sings one note at a time: a key where a note starts gives that note its pitch.
  **SEL**: EDIT WORDS, DELETE NOTE, CLEAR NOTES. **HOME + SEQ** goes deeper, into the words of the line at the
  cursor (stopped); on an empty phrase it starts the line, so the words can come before the notes (the line is
  made when you keep them). **SEL** keeps them; **HOME** cancels; **HOME + SEQ** again keeps them and goes to
  the song.
- **Every syllable needs a note.** While a line you have written to has words no note sings, the phrase can't be
  left (HOME + SEQ to the song, another page, HOME, another track) and the song can't be saved or swapped for
  another. A box says how many notes are missing and how to fix it: KEYS write them (the cursor goes to where
  the next one belongs), or HOME + SEQ to cut the words. Any key or knob puts the box away.

**Drums: the steps.** The white keys are the steps of the drum shown, the black keys pick the drum (and play it).
Tap a step to switch it on or off. Hold a step: a black key puts that drum on it or takes it off, **KNOB 1** is
its velocity, **KNOB 4** its nudge; with nothing held, **KNOB 2** picks the drum shown and **KNOB 3** the bar.
**SELECT** goes to the phrase's bar before or after. **SEL**: CLEAR BAR (the
bar rests).

While the song plays, the keys play the track, and **REC** records what you play into the track's bar playing, on
the nearest step. A bar of rest that gets a note takes a new bar of its own (there are sixteen a track).

### Sound (EDIT)

- **Voice**: BODY, BREATH, EDGE, QUALITY. BODY, BREATH and EDGE change how the words are pronounced:
  sung syllables are made again with them, a moment after you turn. QUALITY is the register it sings in:
  Profondo, Baritone, Tenor, Alto or Soprano (OCT− / OCT+ step it too, with the Voice selected).
- **Keys and Bass**: each has its own sounds, PRESETS stepping through them (the page shows eight
  at a time, the one playing ringed). Bass: deep sub, acid squelch, reese, FM growl, pluck bass,
  wobble (its filter in eighth notes), boom, fat saw and seven more. The knobs are the sound's
  engine's own four. On Bass the keys play two octaves down, F1 to G3.
- **Drums**: PRESETS picks a kit (Studio, Tight, Boom, Dusty, Bright, Crunch). The knobs are TUNE,
  DRIVE, TONE.
- **SEL**: SOUND AS PRESET (every knob back to the preset's).

### Envelope (ENV)

- **Voice**: GLIDE, VIBRATO, RATE. GLIDE is how long the voice slides onto the next note; at 0 it lands.
  A poem starts there, so the words land, and turning it up slides the poet the same way the keys do.
- **Keys and Bass**: ATTACK, RELEASE, CUTOFF, RESO.
- **Drums**: FUNK (each hit pushed a little late), ATTACK, RANDOM (each hit a little different),
  LENGTH.

The screen draws the shape: the rise, the hold, the fall.

### The stage

Wherever Mister Wiggles is out (Sing, the Voice's Sound and Envelope, and FX's karaoke room) he stands on the bottom
bar in front of a wall of glowing CRT cells, with a monitor speaker either side of him. The bars go dark, and their
colour says what is happening: the top bar is plum while stopped, teal while the song plays and red while you record;
a message colours it too (green done, red refused, amber undo), and the effects you hold pile their names on
magenta. While the song plays, the bottom bar shows the words being sung instead of the knobs; turning a knob or
SELECT brings the knobs back for a moment.

What you change shows on the stage, so it never needs a number:

| You change | The stage shows |
|---|---|
| FILTER | The wall's hour: night blues to the left, with rainbow stars; the evening's plums and pinks in the middle; a bright day to the right |
| REVERB | A haze over the wall, its cells glowing into each other |
| TONE | A warmer wall to the left, a cooler one to the right |
| DELAY, REPEATS | Each burst of notes echoes, one echo for each repeat, at the delay's time (while the Voice sends to it) |
| DRIVE | The speakers glowing hot and throwing sparks, the wall running hot |
| PUMP | The speaker cones pushing out and the wall dimming on each kick, as far as the mix ducks |
| BODY | How heavy his shadow is |
| BREATH | Little puffs as he sings |
| EDGE | Laser beams while he sings |
| QUALITY | The size of the music notes: big at the lowest |
| GLIDE | Trails behind the music notes |
| VIBRATO, RATE | His wobble, and the wall waving with it |
| THROAT (the Voice's reverb) | How long the music notes linger |
| A note on any track | He hops and music notes burst off his head in its colour; a high note throws a star too |
| The song playing | He sways and the wall flashes on the beat |

### Karaoke (FX)

Pressing FX enters the karaoke room (the top bar says ENTERING KARAOKE ROOM; leaving says EXITING KARAOKE ROOM). It is
the stage, with:

- **The white keys** punching in effects while held. The effects are the rooms' own: nowhere else do the white keys
  (or HOME and a white key) punch in.
- **The black keys** playing the selected track, as on Sing: the Voice sings, Keys and Bass play (the black keys make
  a minor pentatonic scale, E♭ G♭ A♭ B♭ D♭, so anything fits), Drums plays its eleven drums. ALGORITHM picks the track.
- **SELECT** turning the knobs' bank, and the speakers change colour with it: echoes (REVERB, TONE, DELAY, REPEATS),
  the master (DRIVE, SQUEEZE, PUMP, FILTER), the voice (BODY, BREATH, EDGE, QUALITY), and the voice's motion and reverb
  (GLIDE, VIBRATO, RATE, THROAT: the Voice's reverb send).

Every knob turned in the room throws its own picture: REVERB a ring, TONE a colour wash (warm down, cool up), DELAY
notes in a row, REPEATS rings in rings, DRIVE sparks off the speakers, SQUEEZE stars rushing in to him, PUMP rings off
the speakers, FILTER a band of light sweeping down or up the wall, BODY dust at his feet, BREATH puffs, EDGE lasers,
QUALITY big notes leaping, GLIDE a comet, VIBRATO the wall rippling, RATE a ring of stars, THROAT bubbles from his mouth.

DELAY is the echo's time, in divisions of the beat; REPEATS its feedback; SQUEEZE the master compressor; PUMP the mix
ducking on each kick; FILTER a low-pass to the left, a high-pass to the right, off in the middle.

The punch-in effects come in four families of four, and within a family the key to the right is the stronger:

| Keys | Family | Effects, weaker to stronger |
|---|---|---|
| F3 G3 A3 B3 (1–4) | tone | lo-pass, hi-pass, GAMEBOIII, ring mod |
| C4 D4 E4 F4 (5–8) | chop | chop, choppp, chopppppp, choppppppppp: the sound cut on and off 8, 16, 32 or 64 times a bar |
| G4 A4 B4 C5 (9–12) | brrr | brr, brrrrr, brrrrrrrr, brrrrrrrrrrr: the last eighth, sixteenth, 32nd or 64th repeated |
| D5 E5 F5 G5 (13–16) | | build, crush, lo-fi, tape off |

**Build** is a riser over two bars, on 1 to 4 of the tracks picked at random each time you press it; the top bar
names it with two D's for each track it took (buildd, buildddd, buildddddd, and eight D's for all four). Drums plays a snare roll
that gets faster every two beats (eighths, then sixteenths, 32nds and 64ths) and louder while it thins out from the
bottom up; Keys and Bass climb an octave with their filters opening; the Voice thins out from the bottom up. Let go
and everything drops back in at once.

The brrrs sound wilder than a plain loop: each repeat bounces between the left and the right, and from brrr 16 on
each repeat is a little higher (up to an octave), from brrr 32 a little shorter too.

The first key of each family is lit, to find your place by. Several effects can be held at once. The brrrs and tape
off replay the last quarter second, so only one of them plays at a time: with two held, the lower key wins. The others
work on top of them. The light under the key or keys last pressed blinks after they are let go.

| Effect | The stage shows |
|---|---|
| lo-pass | The wall's top goes dark and deep blue |
| hi-pass | Only the wall's top stays lit, pale, with champagne bubbles fizzing up |
| GAMEBOIII | Everything in two greens, with scanlines |
| ring mod | Rings rippling out from him and the speakers over a ringed violet wall |
| chop | The stage blacked out each time the sound cuts and lit in a new strobe colour each time it opens, with laser beams: two for chop 8, four for 16, six for 32, eight for 64 |
| brrr | The picture frozen and torn into strips at every repeat: shifted for brrr 8, colours swapped from 16, smeared from 32, negative at 64. He shakes with it |
| build | The wall filling up from his feet, flashing on every snare, sparks streaming up, him lifting and trembling; let go, a white flash and a burst of notes |
| tape off | The wall switching off like an old TV, to a line, then a dot, and him falling asleep |
| lo-fi, crush | The wall's cells bigger, and the scene in blocks (crush the bigger) |

### Mixer (LFO)

Each track's level, then (SELECT) its reverb and its echo, KNOB 1–4 for Voice, Keys, Bass,
Drums. Black keys 1–4 mute and unmute. A new song starts with the Voice at 100 and the others at 90.

### Setup (GLO)

The song's own settings, on one page: TEMPO, ROOT and SCALE (the key the keys play in), METER (4/4 or 3/4).
A poem stays in 4/4: the meter knob says so and does not move.
With a scale (Major, Minor, Dorian, Mixolydian, Lydian, Phrygian, Harmonic minor), the white keys play its notes
up from the root and the black keys its pentatonic (major for Major, Mixolydian and Lydian; minor for the others),
so whatever you play or write on the Voice, Keys and Bass is in key; the karaoke room's black keys follow it too.
Chromatic plays the keys as they are. The bottom line says which 386DX build the FM-1 runs.

**SEL** has *RESET KNOBS*: every knob (the effects, the Voice, the tracks' levels, sends and sound knobs) back
to a new song's, and every track's octave home, after a question. Undo brings them back.

The pill under the settings says the mode, **KARAOKE** or **SLAMJAM**: **SELECT** asks to switch, **SEL**
says yes (a song playing stops first). The FM-1 remembers which. It is one firmware: both modes are always there.
A song knows its mode: a poem saved from SLAMJAM loads back into SLAMJAM, its band and players as you left them,
and any other song loads into karaoke. Every song keeps its tempo, key and scale. Loading a song while one plays
stops it first.

## SLAMJAM

A band and a robot poet, making it up: GLO, turn **SELECT**, **SEL**. Your song is kept (as when you load
another) and a poem takes its place; switching back brings your song back. In SLAMJAM the colours turn to
parchment, oxblood and brass, HOME is **Practice** and FX the **SLAMJAM room**.

- **Practice is where the band rehearses, the room is the show.** In Practice (HOME) PLAY plays the band alone and the
  poet keeps quiet; walk into the SLAMJAM room (FX) and he performs, from wherever the band is. PLAY stops and
  starts the poem in the room too, without leaving it.
- **A poem** is a whole performance, about three minutes: the poet says its title in a four-bar intro, then
  four stanzas of four lines with the band answering between them, a stop-time break before the last, quieter
  stanza, and an outro. A refrain comes back; questions rise; the last stanza falls quiet.
- **Every poem is told a different way**: a slam, an ode, a protest, a love letter, a eulogy, an award speech, a
  nature show, a sportscast, a bedtime story, a pep talk, a complaint, a weather forecast or a cooking show. Its
  subject is something ordinary (vegetables, the city, trucks, the arcade, animals, the stuff in your drawers,
  space, dinosaurs, school, the ocean, breakfast, camping, pirates or bugs), and the blanks are filled madlib
  style: mostly from the topic, but now and then from anywhere (a rubber chicken, the cheese aisle, my grandma,
  at three in the morning), so an ode to a parking meter can end up smelling of cheese puffs. Each poem also
  has a core it keeps coming back to: food, tools, gadgets, astronomy, chemistry, flora, fauna or nautical
  things, so one love letter thinks of you whenever it eats nachos and the next whenever it eats capacitors. A poem
  has a lead core, which most of its blanks come from, and a support, which a third of them do: any core, the
  lead's own as likely as any other (a pure poem). In the room each core's thing stands on a table by a candle (a
  slice of pizza, a hammer, a microchip, a ringed planet, a flask, a tulip, a penguin, a life ring): the lead's on
  the left, the support's on the right.
- **Every poem has a band**, and with it a tempo, a key and a feel:

  | Band | Tempo | Feel |
  |---|---|---|
  | jazz bar | 80 to 96 | minor: rhodes, walking bass, the ride a little late |
  | boom bap | 76 to 84 | minor: stabs, a held bass, the snare leaning; the poet raps |
  | beatnik | 100 to 112 | minor: vibes, walking bass, bongos, a loose ride |
  | funk | 98 to 110 | dominant: organ, sixteenth bass, the hats poking |
  | bossa | 108 to 120 | major: bossa comping and bass, the rim's clave, pushed; the poet sings |
  | lo-fi | 70 to 82 | minor: a pad, a held bass, brushes, everything a little late; the poet sings |
  | trap | 66 to 74 | minor, straight, slow: an arp, a sliding 808 on the kick, the clap on two and four, hats in sixteenths with triplet rolls; the poet raps |
  | punk | 108 to 120 | major: power chords, eighth-note bass, rock drums, the quarters hit harder |

- **The four knobs in Practice are the four players' styles**, changed while the band plays (the name is said
  as you turn); in the room the fifth knob bank (SELECT) is the same four, to change the band during the show. A new poem's band sets them; turn any of them to make it your own:

  | Knob | Player | Styles |
  |---|---|---|
  | 1 | POET | chant, sing (long notes on the chords, kept in the reciting range), rap (fast, on the sixteenths) |
  | 2 | KEYS | rhodes, stabs, pad, arp, organ, vibes, bossa, power, yours |
  | 3 | BASS | walk, boom, pedal, funk, 808, bossa, punk, yours |
  | 4 | DRUMS | boom bap, swing, brushes, double, bossa, trap, bongos, rock, yours |

  Each style brings its own sound (a kit for the drums) and its own level, so the band stays under the poet.
  Turning the drums rewrites the keys and the bass so they land on the new kicks (a part set to yours is left
  alone, and the screen says the band follows). Turning the poet to rap brings the tempo down to 84 when it
  was faster, and leaves it there, so a sixteenth still lands. Offbeat hats and the cymbal wait until that
  chord comes around again; the quarters always play.
  **yours** (the last on each) leaves that part to you: the band stops playing it, its sound and level stay as you
  set them, and what you play with REC lit is written there and kept, coming round with the band's chords. Turning
  to another style writes the band's part over it again (HOME + REC undoes that). GLO's ROOT moves the whole poem to
  the new key, the band and the poet with it (your own parts stay as you played them).
- **On the stage, SEL**: *NEW POEM* (the same topic; a new way of telling it, a new band and new words) or
  *TOPIC* (the next one). The header says what kind of poem it is (*A NEW EULOGY*), the strip which band. The
  poem is the song now: PLAY plays it round, the sequencer shows it, and **SAVE TO** keeps it in a slot.
- **Jam**: HOME and a black key 1 to 4 takes that track from the band (it goes quiet) or gives it back; turn
  **ALGORITHM** to that track and the keys are yours. In the room, on the Voice, the black keys give the poet a
  tune: the notes you play gather in the top bar, and when there are as many as the next line has syllables he sings
  that line to them, an octave folded down or up into his own low-middle range. He keeps his words and his time; you
  give the tune. In Practice the Voice's keys sing "ah".
- **The SLAMJAM room** (FX) is the show: walking in starts the poem, walking out stops it. Its header shows the
  track your black keys play and its sound; **PRESETS** changes the sound (the kit for Drums) there too. Its
  white keys:

  | Keys | Effects |
  |---|---|
  | 1 to 4 | **wah** (a resonant sweep, twice a beat), **echo** (a dub echo on the triplet eighth: held, it builds; let go, it rings on), **scratch** (the last sixteenth rocked back and forth like a record), **air horn** (three blasts, the last held) |
  | 5 to 8 | the chops, as in the karaoke room |
  | 9 to 12 | **snaps**: the room snapping its fingers 4, 8, 16 or 32 times a bar |
  | 13 to 16 | build, crush, lo-fi, tape off, as in the karaoke room |

- **Mister Wiggles** wears a beret here, heavy lids while he speaks, and opens his eyes wide on a loud word. On
  in Practice the house lights are up: warm brick and no sign, the band rehearsing. In the room it is dark, a spotlight on him,
  the sign lit, and he wears shades while the band plays alone. The wah rides up and down the wall, the echo
  says the sign again under itself, the scratch drags the room with the record, the air horn rings out over
  everyone, and the snaps throw sparks off the tables.

Every word the poet can say is in the firmware's own list, so it is family-friendly by construction.

### Songs (SAVE)

**SAVE** opens the songs: *LOAD*, *SAVE TO* and *CLEAR SLOT* each open the eight slots, every one with its
song's name (or *empty*, and a dot on the song you are in): **SELECT** moves, **SEL** does it, **HOME** goes
back. *LOAD* on an empty slot starts a new song there; the song you had is kept first. *CLEAR SLOT* empties
a slot after a question (*CLEAR S3?*); clearing the song you are in starts a new one in its place. *RENAME
SONG* edits the name.

**SAVE held** saves the song where it lives (a song with no slot yet opens *SAVE TO*). **SAVE held with
white key 1–8** saves it to that slot. There is no autosave: SAVE is lit while the song has changes not
saved yet.

### Arp (ARP)

Keys' and Bass' arpeggiator. With a TYPE set, the notes held (on the keys, or a step's chord while it lasts) play
one at a time, as long as they are held:

- **TYPE** (KNOB 1): off, up, down, bounce (up and down again), random, played (in the order the keys went down).
- **RATE** (KNOB 2): 1/4, 1/8, 1/8T, 1/16, 1/16T, 1/32.
- **RANGE** (KNOB 3): one to four octaves, the notes again an octave up for each.
- **GATE** (KNOB 4): how long each note sounds, in percent of the rate.

The screen draws what a held three-note chord would play. Keys and Bass each keep their own; the Voice and Drums
don't arpeggiate.

## The companion page

Open `web/index.html` from a local web server in Chrome or Edge, with the FM-1 plugged in.
`tools/build.py` puts the firmware beside the page; `tools/make_companion.py` makes the folder to
publish.

- **Songs on the FM-1** (shown once the page is connected to an FM-1 running 386DX): the eight slots, each with what it holds and a menu of what should happen
  to it: one of our songs (thirteen public-domain standards) or *Import a file…* (a karaoke .kar or
  MIDI .mid file with its words as timed lyrics) put there, *Download it*, or *Delete it*. A file chosen to
  import has its key found from its notes (*It sounds like D minor*), shown with a menu for the note and one for
  the scale: keep it or change it, then *Use this song*. The song's ROOT and SCALE are set to it, so the keys play
  in the song's key and the rooms' black keys its pentatonic; our songs get their key found the same way. A song saved by
  386DX 1.0 shows *Update it*, chosen already: it becomes this version's song (each track its own chain of
  four-bar phrases, the Voice's phrases starting where its lines do) with nothing else changed. The FM-1 lists
  1.0's songs but won't load one until it is updated; a download of one comes down updated. Each
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

A song holds 128 bars, a chain of up to 128 bars a track, 64 lines, 96 places a line starts, 512 sung notes and
16 bars of material a track. The FM-1 keeps 8 songs.
Words are up to 63 letters a line; a line has at most 64 syllables. A digit in a lyric is read
one digit at a time: spell numbers out.
