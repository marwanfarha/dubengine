# dubengine controls (v0.2)

How the FM1's controls work for picking a key, keeping time and recording chord patterns. Built and tested in the emulator; saving patterns and the Digitakt's clock come next.

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
| **ENV** | The sound pages in turn: Osc, Filter, Amp env, Filter env, Env 1, Env 2. |
| **LFO** | LFO 1, LFO 2. |
| **FX** | Delay, Reverb, in turn. |

EDIT, SAVE and ARP are left for Randomise and the FM pings later.

## Pages and knobs

| Page | KNOB 1 | KNOB 2 | KNOB 3 | KNOB 4 |
| --- | --- | --- | --- | --- |
| **Play** (HOME) | Tempo | Root (wraps round) | Mode (Ionian to Aeolian; Locrian to come) | Voicing (Root, 1st, 2nd) |
| **Pattern** (SEQ) | Length (1 to 64 steps) | Step value (1/8, 1/16, 1/32, 1/8T, 1/16T) | Metronome level | (free) |

| **Osc** (ENV) | Wave (sine, triangle, saw, square) | Fold | Detune | Level |
| **Filter** (ENV) | Cutoff | Reso | Env amount (±) | Key track |
| **Amp env** (ENV) | Attack | Decay | Sustain | Release |
| **Filter env** (ENV) | Attack | Decay | Sustain | Release |
| **Env 1, Env 2** (ENV) | Attack | Decay | Sustain | Release |
| **LFO 1, LFO 2** (LFO) | Rate | Shape (sine, triangle, saw, square, random) | Target | Depth (±) |
| **Delay** (FX) | Time (1/16 to 1 bar, dotted and triplets) | Feedback (0 to 110%) | Mix | Tone |
| **Reverb** (FX) | Decay | Mix | Pre-delay (1 to 100 ms) | Damping |

On Env 1 and Env 2, ALGORITHM picks the target and PRESETS the depth (±). On the LFOs, ALGORITHM syncs the rate to the tempo (Free, 4 bars to 1/16). On Delay, ALGORITHM is Wow (tape sway and flutter) and PRESETS is Drive (tape saturation). On Reverb, ALGORITHM is Low cut and PRESETS is Width.

Mix on both: 0 is dry only, 50 is dry and wet at full, 100 is wet only. Feedback past 100% makes the delay swell on itself (the tape saturates, so it doesn't run away).

Targets: Pitch, Fold, Detune, Cutoff, Reso, Level, Amp attack, Amp decay, Amp release, Delay mix.

Each sound page draws what it sets: the wave, the filter's curve, the envelope, the LFO, the delay's repeats, the reverb's tail.

MASTER is volume. SELECT isn't used yet.

## Live recording (playing, REC on)

* Lit keys play chords as always, and each one is written into the pattern.
* A chord snaps to the nearest step. Its length is how long the key was held, rounded to whole steps, at least one.
* A new chord replaces whatever was on the steps it covers (overdub replaces, it doesn't stack).
* Recording starts the moment you press REC, since the pattern is already playing, so there's no count in.

## Step edit (stopped, REC on)

* The 16 white keys are steps. Lit: a chord is on that step. The step playing blinks.
* Tap a white key: put a chord on that step, or take it off. A new step gets the last chord you set or played.
* While stopped, the chord sounds as you press or change a step.
* Hold a step and turn a knob to lock that step:
  * KNOB 1: chord (the scale's degrees, shown by name: Cm, D dim, Eb ...)
  * KNOB 2: voicing (Key: the Play page's, or Root, 1st, 2nd for this step)
  * KNOB 3: octave (−2 to +1)
  * KNOB 4: length in steps
* Black keys do nothing in step edit.
* OCT− / OCT+ move between step pages when the pattern is longer than 16.

## Screen

* **Header:** key (C Aeolian), tempo, metronome icon, REC or EDIT tag.
* **Middle:** the chord playing, large. Under it, the pattern as a row of steps with the playhead.
* **Footer:** the page's four knob names and values, as now.

## Decided

1. Metronome on SEL (can move later).
2. A new step gets the last chord set.
3. Live overdub replaces.

## Not yet

* Saving patterns (the key and tempo are saved; the pattern isn't yet).
* Following the Digitakt's clock.
* Locrian, step locks for level, filter and echo send.
* Saving the sound settings (they go in with pattern saving).
