# dubengine spec (v0.1)

A chord sequencer for the FM1. You pick a key, play one key per chord, and record chord patterns to a metronome or the Digitakt's clock. DUB 1's generative parts are removed.

## 1. Key

* Any root note (C to B) and any of the 7 modes: Ionian, Dorian, Phrygian, Lydian, Mixolydian, Aeolian, Locrian.
* Default: C Aeolian (C minor).
* **Only the keys in the scale light up.** Keys outside the scale are dark and do nothing.

## 2. Playing

* One key plays one triad, built from the scale (diatonic). In C minor: C = Cm, D = D dim, Eb = Eb, F = Fm, G = Gm, Ab = Ab, Bb = Bb.
* The chord sounds for as long as the key is held.
* **Voicing** (global setting): Root position, 1st inversion, 2nd inversion, or Smooth (the inversion closest to the previous chord). Default: 1st inversion.
* One chord at a time: a new chord replaces the one playing.

## 3. Tempo and metronome

* Internal tempo with a metronome (on or off, with a level).
* When MIDI clock comes in (TRS or USB, from the Digitakt), dubengine follows its tempo, start and stop.

## 4. Sequencer

* Pattern length: 1 to 64 steps.
* Step value: 1/16 by default. Options: 1/8, 1/16, 1/32, 1/8 triplet, 1/16 triplet.
* **Live recording:** arm, play along to the metronome. Chords snap to the nearest step. Their length is recorded in steps, from how long you held the key.
* **Step entry:** select a step, press a key to place a chord there, or clear it.
* **Step locks** (like Elektron parameter locks): hold a step and turn a knob to change that step only. Lockable per step: voicing, octave, level, filter cutoff, echo send. Unlocked steps use the global settings.
* Clear pattern.
* Patterns are saved to flash.

## 4b. Randomise

* **RANDOMISE** writes a new pattern into the sequencer, using the current key, length and step value. The result is a normal pattern: you can edit it, record over it and save it.
* Controls that shape what it writes:
  * **Density:** how busy the pattern is, from a few chords to a chord on most steps.
  * **Variation:** how many different chords it uses, from staying on the root chord to using every chord in the key.
  * **Length:** short stabs to long held chords.
  * **Offbeat:** on the beat to pushed onto the offbeats (the classic dub chord position).
* **Evolve:** how much the pattern changes by itself each time it loops. 0 means it stays fixed.
* Randomise can be undone once, back to the pattern before.

## 5. Sound

* **Osc:** each of the chord's voices two oscillators, detuned: sine, triangle, saw or square. A wavefolder after them.
* **Filter:** one resonant low pass for the chord: cutoff, reso, envelope amount, key track.
* **Envelopes:** ADSR for the amp, for the filter, and two more (Env 1, Env 2) to any target.
* **LFOs:** two, sine, triangle, saw, square or random; free or synced to the tempo; to any target.
* **Targets:** pitch, fold, detune, cutoff, reso, level, amp attack, decay and release, delay mix.
* **Delay** (DUB 1's tape echo): time synced to the tempo, feedback in %, mix, tone, wow and flutter, drive.
* **Reverb** (DUB 1's plate): decay, mix, pre-delay, damping, low cut, width.

## 5a. Kept from DUB 1

* Tape echo (feedback, hold).
* Plate reverb.
* FM pings: kept in the code, off by default, to switch on later.

## 6. Removed from DUB 1

* DUB 1's always on generative chord walk and random voicing (replaced by Randomise and Evolve above).
* Sub bass, kick, shaker, hats, vinyl crackle.
* The 8 bar arrangement cycle, breaks and fills.

## 7. Build order

1. Support both flash chips (`0x856014` and `0xB36014`), so saves work on every FM1.
2. Strip the generative parts. Keep chord voice, filter, echo, reverb.
3. Key: root, mode, key lights, out of scale keys ignored.
4. Tempo, metronome, MIDI clock in.
5. Sequencer: length, step value, live recording, step entry, step locks, clear.
6. Randomise and Evolve.
7. Saving patterns.

Each step: tests and simulator pass, then the emulator, then the hardware.

## Open questions

* Which buttons and knobs do what (layout to be proposed after reading DUB 1's panel code).
* 7th and 9th chords as an option later.
