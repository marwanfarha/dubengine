# dubengine controls (draft v0.1)

How the FM1's controls work for picking a key, keeping time and recording chord patterns. Draft for review: nothing here is built yet.

## The user's flow

1. Pick a key (root and mode).
2. Turn the metronome on.
3. Set how long the pattern is.
4. Record: live while it plays, or step by step while stopped.
5. Fix single steps (chord, voicing, octave, length).
6. Save.

## Buttons

| Control | Does |
| --- | --- |
| **PLAY** | Start and stop. |
| **REC**, while playing | Live recording on or off. PLAY lights red while recording. |
| **REC**, while stopped | Step edit on or off. REC stays lit. PLAY still starts and stops inside step edit. |
| **SEL** | Metronome on or off. Lit while on. |
| **HOME** | Play page (key, tempo, voicing). |
| **SEQ** | Pattern page (length, step value). |
| **SEQ** held 1.5 s | Clear the pattern. The screen says CLEARED, and SEQ held again within 5 s brings it back. |
| **GLO** held | Save. |
| **OCT− / OCT+** | Outside step edit: the keys' octave. In step edit: the step page (1 to 16, 17 to 32, and so on). |

The other buttons (FX, ENV, LFO, EDIT, SAVE, ARP) are left for the sound pages, Randomise and the FM pings later.

## Pages and knobs

| Page | KNOB 1 | KNOB 2 | KNOB 3 | KNOB 4 |
| --- | --- | --- | --- | --- |
| **Play** (HOME) | Tempo | Root | Mode | Voicing |
| **Pattern** (SEQ) | Length (1 to 64 steps) | Step value (1/8, 1/16, 1/32, 1/8T, 1/16T) | Metronome level | Count in (off, 1 bar) |

SELECT stays the DJ filter. MASTER is volume.

## Live recording (playing, REC on)

* Lit keys play chords as always, and each one is written into the pattern.
* A chord snaps to the nearest step. Its length is how long the key was held, rounded to whole steps, at least one.
* A new chord replaces whatever was on the steps it covers (overdub replaces, it doesn't stack).
* With the metronome on and Count in set, recording starts after one bar of clicks.

## Step edit (stopped, REC on)

* The 16 white keys are steps. Lit: a chord is on that step. The step playing blinks.
* Tap a white key: put a chord on that step, or take it off. A new step gets the last chord you set.
* Hold a step and turn a knob to lock that step:
  * KNOB 1: chord (the scale's degrees, shown by name: Cm, D dim, Eb ...)
  * KNOB 2: voicing (root, 1st, 2nd)
  * KNOB 3: octave (−1, 0, +1)
  * KNOB 4: length in steps
* Black keys do nothing in step edit.
* OCT− / OCT+ move between step pages when the pattern is longer than 16.

## Screen

* **Header:** key (C Aeolian), tempo, metronome icon, REC or EDIT tag.
* **Middle:** the chord playing, large. Under it, the pattern as a row of steps with the playhead.
* **Footer:** the page's four knob names and values, as now.

## Open questions

1. Metronome on SEL, or somewhere else?
2. A new step in step edit: the last chord set, or always the root chord?
3. Live overdub: replace (as above), or keep the old chord too?
