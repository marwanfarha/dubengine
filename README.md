# dubengine

Custom firmware for the **M VAVE FM1**: two tracks of dub chords for dub techno, minimal and microhouse.

**[▶ Play it in your browser](https://marwanfarha.github.io/dubengine/)**, the same code the FM1 runs. Turn your phone sideways for bigger controls.

**[Install on your FM1](https://marwanfarha.github.io/dubengine/install/)** (Chrome or Edge on a computer, FM1 plugged in by USB).

## What it does

* **Two tracks, A and B.** Each has its own key and scale, pattern, synth, delay and EQ. They share the tempo and one reverb.
* **One finger chords.** Each lit key plays the triad its track's scale builds on it. Root position, 1st or 2nd inversion.
* **Patterns up to 64 steps.** Record live or step by step. Each track has its own length, so 16 steps against 12 keeps shifting.
* **Step locks.** Chord, voicing, octave, length, level, cutoff and delay mix on any step.
* **A full synth a track.** Two oscillators a voice (sine, triangle, saw, square), wavefolder, resonant filter, four envelopes, two LFOs.
* **Tape delay a track, one plate reverb** with a send from each track.
* **Master page.** Levels, meters and an 8 band EQ for each track.
* **Randomise** the pattern, the sound or the delay, once, by hand. Each roll can be undone.
* **8 pattern slots and 8 sound slots.**
* **Kept for later:** a bass voice and an FM ping voice, waiting for their sequencers.

## Install on your FM1

1. On a computer, open **[the installer](https://marwanfarha.github.io/dubengine/install/)** in Chrome or Edge.
2. Plug the FM1 straight into the computer with a USB data cable. Turn it on. Close any other app that uses MIDI.
3. Press **Install**. Allow MIDI when the browser asks.
4. Wait about two minutes. Do not unplug or close the page. The FM1 restarts by itself.
5. The installer checks the FM1 now reports dubengine. Done.

Works from the stock firmware or DUB 1.

**Going back:** "Back to the stock firmware" at the bottom of the installer page, or M VAVE's own updater, M UPGRADE.

**Update mode by hand:** hold OCT− and OCT+ together for 5 seconds. A countdown shows; let go to cancel.

**If it ever won't start:** after two failed starts it comes up in SAFE MODE, which the installer can still reach. Last resort: the [Mac rescue script](https://sbegueria.github.io/fm1-dub/manual/#recovering-an-fm-1-that-won-t-start).

## How to use it

### Quick start
1. Press a **lit key**. It plays the chord the scale builds on that note. Dark keys are outside the scale.
2. **HOME**: KNOB 1 tempo, KNOB 2 root, KNOB 3 mode, KNOB 4 voicing.
3. **HOME again**: the other track. The header shows **A** or **B**. Everything you play and edit goes to that track.
4. **SEL**: metronome on or off.
5. **PLAY**, then **REC**: play lit keys to record. **REC** again to stop recording.
6. **ENV**, **LFO**, **FX**: shape the sound. Press again for the next page.
7. **GLO**: levels and EQ.

### Buttons

| Button | Tap | Hold |
| --- | --- | --- |
| PLAY | Start and stop both tracks. | |
| REC | Playing: live recording on or off. Stopped: step edit on or off. | |
| SEL | Metronome on or off. | |
| HOME | Play page. Again: the other track. | |
| SEQ | Pattern page. | 2.5 s: clear the pattern (a countdown shows). Again within 5 s: bring it back. With a white key: pattern slots. |
| ENV | Osc, Filter, Amp env, Filter env, Env 1, Env 2, in turn. | With a white key: sound slots. |
| LFO | LFO 1, LFO 2. | |
| FX | Delay, Delay 2, Reverb, Reverb 2, in turn. | |
| GLO | Master, EQ of track A, EQ of track B, in turn. | 1 s: save now. |
| EDIT | Random page. Again: a new pattern. | 1 s: undo the pattern. |
| SAVE | Nudge the sound a little. | 1 s: a whole new sound. |
| ARP | A new delay for this track. | 1 s: undo the last sound or delay change. |
| OCT− OCT+ | The keys' octave. In step edit: the next or previous 16 steps. | Both for 5 s: update mode. |

Everything saves by itself 3 seconds after you stop.

### Pages

| Page | KNOB 1 | KNOB 2 | KNOB 3 | KNOB 4 | ALGORITHM | PRESETS |
| --- | --- | --- | --- | --- | --- | --- |
| Play | Tempo | Root | Mode | Voicing | | |
| Pattern | Length | Step | Click level | | | |
| Osc | Wave | Fold | Detune | Level | | |
| Filter | Cutoff | Reso | Env amount | Key track | | |
| Amp env | Attack | Decay | Sustain | Release | | |
| Filter env | Attack | Decay | Sustain | Release | | |
| Env 1, 2 | Attack | Decay | Sustain | Release | Target | Depth |
| LFO 1, 2 | Rate | Shape | Target | Depth | Sync | |
| Delay | Time | Feedback | Mix | Tone | Wow | Drive |
| Delay 2 | Width | Blur | Mode | Ping pong | | |
| Reverb | Decay | Size | Pre delay | Mix | Width | High cut |
| Reverb 2 | Damping | Diffusion | Mod depth | Mod rate | Low cut | Freeze |
| Master | Level A | Level B | | | | |
| EQ | Band | Freq | Gain | Q | | |
| Random | Density | Variation | Length | Offbeat | | |

* **Tempo** is shared. **Root, Mode and Voicing** belong to the selected track.
* **Modes:** Major, Dorian, Phrygian, Lydian, Mixolydian, Minor, Locrian.
* **Delay:** SELECT switches the time between Sync (1/16 to 1 bar) and Free (20 ms to 2 s). The longest delay is 3 seconds (1.5 with ping pong).
* **Feedback:** below 85 the repeats thin and darken with Tone. At 100 they hold forever. Above 100 they swell into tape saturation and stay.
* **Mode** (what happens when the delay time changes): Repitch bends the repeats like a tape, Fade crossfades with no bend, Jump switches at once.
* **Ping pong:** repeats alternate left and right.
* **Mix:** 0 dry only, 50 dry and wet both full, 100 wet only.
* **Reverb:** one reverb for both tracks. Mix is each track's own send. The other reverb knobs are shared.
* **Targets** for envelopes and LFOs: pitch, fold, detune, cutoff, reso, level, amp attack, decay and release, delay mix.

### Master and EQ
* **Master:** a meter for A, B and the output, in dB, with peak hold. Level 80 is normal, 100 is 4 dB louder. The processor load shows under the meters.
* **EQ:** 8 bands a track. KNOB 1 picks the band, then set its frequency, gain and Q.

| Band | Type | Controls |
| --- | --- | --- |
| 1 | Low cut | Freq (fully left: off) |
| 2 | Low shelf | Freq, Gain |
| 3 to 6 | Bell | Freq, Gain, Q |
| 7 | High shelf | Freq, Gain |
| 8 | High cut | Freq (fully right: off) |

Gain is ±15 dB. The picture shows the whole curve from 20 Hz to 20 kHz.

### Step edit
1. Stop, then press **REC**. The 16 white keys are the steps.
2. Longer than 16 steps: turn **ALGORITHM** (or press OCT− OCT+) for the next 16. The underline shows which row the keys edit.
3. Tap a step: add your last chord, or remove it.
4. Hold a step and turn:

| Control | Lock |
| --- | --- |
| KNOB 1 | Chord |
| KNOB 2 | Voicing |
| KNOB 3 | Octave |
| KNOB 4 | Length |
| ALGORITHM | Level |
| PRESETS | Cutoff |
| SELECT | Delay mix |

5. Level, Cutoff and Delay mix start from the page's value. Turn one down past 0 to free it again. A locked step shows a small dot.
6. **REC** again to leave.

### Slots
8 pattern slots and 8 sound slots. Either track can use any slot.

1. Hold **SEQ** (patterns) or **ENV** (sounds). The first 8 white keys are the slots. Lit keys have something in them.
2. **Tap** a key: load that slot into the selected track.
3. **Hold** a key for a second: save the selected track into that slot.

* A sound slot holds the synth, the delay, the reverb send and the EQ. It leaves the level and the shared reverb alone.
* Loading can be undone: EDIT held for a pattern, ARP held for a sound.
* Saving a slot stops the sound for a moment.

### Randomise
Nothing changes by itself. Each press is one roll, on the selected track.

| Press | Does | Undo |
| --- | --- | --- |
| EDIT (on the Random page) | A new pattern in the track's key | EDIT held |
| SAVE | Nudges every synth knob a little | ARP held |
| SAVE held | A whole new sound, in ranges that suit dub chords | ARP held |
| ARP | A new delay and reverb send | ARP held |

* **Density:** how many chords.
* **Variation:** how many different chords.
* **Length:** how long chords are held.
* **Offbeat:** left, on the beat. Right, off the beat.

### The screen
* **Header:** the page, the track, its key, play and record, the metronome, the tempo.
* **Picture:** what the page sets, to scale and live. LFOs across one bar, envelopes on a time axis, the oscillators as heard, the filter's curve where the cutoff is now, the delay and reverb in seconds, the EQ curve, the meters.
* **Footer:** the four knobs, each with a bar showing where it sits. The one you turn lights orange and shows large for a moment.

## Next

| Feature | Status |
| --- | --- |
| Digitakt clock sync | Planned |
| Sequencers for the bass and the FM ping | Planned |
| Keep the metronome click out of recordings | Planned |

## Working on it

1. On this page: **Code > Codespaces > Create codespace on main**. Setup installs everything (a few minutes the first time).
2. In its terminal:

| Command | What it does |
| --- | --- |
| `./dev.sh test` | All tests, no hardware needed |
| `./dev.sh emu` | The emulator from your own changes, in your browser |
| `./dev.sh build` | The real firmware file: `build/amb.fwsc` |
| `./dev.sh check` | Build plus tests. Run this before anything goes on the FM1. |

GitHub runs the tests and builds the real firmware on every push (the **Actions** tab). A push to `main` also republishes the emulator and the installer, with the version in the `VERSION` file.

## Installing from the command line

With the FM1 connected by USB:

```
pip install mido python-rtmidi
python tools/fm1_install.py --info
python tools/fm1_install.py build/amb.fwsc
```

## Safety

1. **Test first.** `./dev.sh check` and the emulator before the hardware.
2. **Keep the stock firmware.** FM1 V15 ("PC Firmware") from m-vave.com/download.
3. **Never touch the bootloader or update loader** (`firmware/loader/`, `tools/fm1pkg_make.py`). Only the app changes.
4. **Both flash chips are supported** (`0x856014`, and `0xB36014` on newer units). After the first install, check a save survives switching off and on.
5. **Recovery, in order:**
   * Won't start: hold OCT minus while switching on, then install V15 or any firmware over USB.
   * PC only shows a USB disk "WL82 UBOOT1.00": [M Flasher](https://github.com/jvitkauskas/m-flasher) or [MvaveFM1Unbricker](https://github.com/Quixotic7/MvaveFM1Unbricker).
   * Never shows that disk: [FM-1 Transporter](https://github.com/kurogedelic/FM-1-transporter).
   * Full guide: [Firmware Hub rescue](https://thefirmwarehub.com/tools/rescue/).

## Where things are

| Path | What |
| --- | --- |
| `firmware/src/dsp/amb.c` | The sound engine: two tracks, synth, delay, reverb, EQ, sequencer |
| `firmware/src/app/de_ui.c` | The panel: controls, screen, saving, slots, randomise |
| `firmware/hal/`, `firmware/loader/` | Hardware and update loader. Don't change. |
| `host/` | Simulator that runs the app on a computer from a script |
| `web/emu/` | Browser emulator |
| `tests/` | Tests and scenario scripts |

## Credits

dubengine is built on **DUB 1** by sbegueria ([source](https://github.com/sbegueria/fm1-dub)), a fork of Charles Vestal's AMB 1. Its platform layer (hardware, USB, update loader, storage, installer) comes from Leo Kuroshita's **Felucca**. The full history is kept in this repo. DUB 1's tape echo, plate reverb and FM voices live on here; its generative layers were removed.

Licence: GPL 3.0 only, as upstream. See [LICENSING.md](LICENSING.md).
