# dubengine

Custom firmware for the **M VAVE FM1** that turns it into a chord sequencer for dub techno, minimal and microhouse.

**[▶ Play it in your browser](https://marwanfarha.github.io/dubengine/)**, the same code the FM1 runs. Turn your phone sideways for bigger controls.

**[Install on your FM1](https://marwanfarha.github.io/dubengine/install/)** (Chrome or Edge on a computer, FM1 plugged in by USB).

## The idea

Dub techno lives on chord stabs: one shape moved around a key, thrown into a tape echo and a big reverb. dubengine makes the FM1 do exactly that, and play alongside an Elektron Digitakt II.

1. **Pick a key.** Any root note, any mode. Only the keys in your scale light up.
2. **Play chords with one finger.** Each lit key plays the triad your scale builds on it. Root position, 1st or 2nd inversion.
3. **Record a pattern.** Turn the metronome on, set the length, and record live. Or stop and enter chords step by step.
4. **Shape each step.** Lock the chord, voicing, octave and length of any step, like Elektron parameter locks.
5. **Let it move.** Randomise writes a new pattern in your key. Density, Variation, Length and Offbeat shape it, and Evolve lets it change by itself as it loops.
6. **Sync it.** It runs on its own tempo, or follows the Digitakt's clock when they're connected.

Every chord comes from its own synth: two oscillators a voice (sine, triangle, saw or square), a wavefolder, a resonant filter, amp and filter envelopes, two more envelopes and two LFOs to modulate almost anything. Then a tape echo and a plate reverb.

## Install on your FM1

1. On a computer, open **[the installer](https://marwanfarha.github.io/dubengine/install/)** in Chrome or Edge.
2. Plug the FM1 straight into the computer with a USB data cable. Turn it on. Close any other app that uses MIDI.
3. Press **Install**. Allow MIDI when the browser asks.
4. Wait about two minutes. Do not unplug or close the page. The FM1 restarts by itself.
5. The installer checks the FM1 now reports dubengine. Done.

Works from the stock firmware or DUB 1. Your saved pattern and sound live in their own flash area.

**Going back:** "Back to the stock firmware" at the bottom of the installer page, or M VAVE's own updater, M UPGRADE.

**Update mode by hand:** hold OCT− and OCT+ together for 5 seconds. A countdown shows; let go to cancel.

**If it ever won't start:** after two failed starts it comes up in SAFE MODE, which the installer can still reach. Last resort: the [Mac rescue script](https://sbegueria.github.io/fm1-dub/manual/#recovering-an-fm-1-that-won-t-start).

## How to use it

### Quick start
1. Press a **lit key**. It plays the chord your scale builds on that note. Dark keys are outside the scale.
2. **HOME**: KNOB 1 tempo, KNOB 2 root, KNOB 3 mode, KNOB 4 voicing.
3. **SEL**: metronome on or off.
4. **PLAY**, then **REC**: play lit keys to record. **REC** again to stop recording.
5. **ENV**, **LFO**, **FX**: shape the sound. Press again for the next page.

### Buttons

| Button | Does |
| --- | --- |
| PLAY | Start and stop the pattern. |
| REC | Playing: live recording on or off. Stopped: step edit on or off. |
| SEL | Metronome on or off. |
| HOME | Play page. |
| SEQ | Pattern page. Hold 1.5 s: clear the pattern. Hold again within 5 s: bring it back. |
| ENV | Osc, Filter, Amp env, Filter env, Env 1, Env 2, in turn. |
| LFO | LFO 1, LFO 2. |
| FX | Delay, Reverb, Reverb 2, in turn. |
| GLO | Hold: save now. Saving also happens by itself after 3 s stopped. |
| OCT− OCT+ | The keys' octave. In step edit: the next or previous 16 steps. |

### Pages

| Page | KNOB 1 | KNOB 2 | KNOB 3 | KNOB 4 | ALGORITHM | PRESETS |
| --- | --- | --- | --- | --- | --- | --- |
| Play | Tempo | Root | Mode | Voicing | Step edit: steps 1 to 16, 17 to 32... | |
| Pattern | Length | Step | Click level | | Step edit: steps 1 to 16, 17 to 32... | |
| Osc | Wave | Fold | Detune | Level | | |
| Filter | Cutoff | Reso | Env amount | Key track | | |
| Amp env | Attack | Decay | Sustain | Release | | |
| Filter env | Attack | Decay | Sustain | Release | | |
| Env 1, 2 | Attack | Decay | Sustain | Release | Target | Depth |
| LFO 1, 2 | Rate | Shape | Target | Depth | Sync | |
| Delay | Time | Feedback | Mix | Tone | Wow | Drive |
| Reverb | Decay | Size | Pre delay | Mix | Width | High cut |
| Reverb 2 | Damping | Diffusion | Mod depth | Mod rate | Low cut | Freeze |

* Delay: **SELECT** switches the time between Sync (1/16 to 1 bar) and Free (20 ms to 2 s).
* Mix: 0 dry only, 50 dry and wet both full, 100 wet only.
* Targets: pitch, fold, detune, cutoff, reso, level, amp attack, decay and release, delay mix.

### Step edit
1. Stop, then press **REC**. The 16 white keys are the steps.
2. Longer than 16 steps: turn **ALGORITHM** (or press OCT− OCT+) for the next 16. The underline shows which row the keys edit.
3. Tap a step: add your last chord, or remove it.
4. Hold a step and turn: KNOB 1 chord, KNOB 2 voicing, KNOB 3 octave, KNOB 4 length.
5. **REC** again to leave.

### The screen
* **Header:** the page, the key, play and record, the metronome, the tempo.
* **Picture:** what the page sets, to scale and live. LFOs across one bar, envelopes on a time axis, the oscillators as heard (detune, fold and modulation included), the filter's curve where the cutoff is now, the delay and reverb in seconds.
* **Footer:** the four knobs, each with a bar showing where it sits. The one you turn lights orange and shows large for a moment.

## Where it is now

| Feature | Status |
| --- | --- |
| Keys play chords in the chosen key, held while pressed | Done |
| Saves work on both FM1 flash chips | Done |
| Metronome, pattern length, step value, live recording | Done |
| Step edit, step locks for chord, voicing, octave, length | Done |
| Synth: 4 waves, fold, filter, amp and filter envelopes, 2 mod envs, 2 LFOs | Done |
| Delay page (sync or free time, feedback, mix, tone, wow, drive) and two Reverb pages (decay, size, pre-delay, mix, width, high cut, damping, diffusion, modulation, low cut, freeze) | Done |
| Black and orange screen, live to scale pictures | Done |
| Real firmware built and update path tested on every push | Done |
| Saving the pattern and the sound (by themselves, or GLO held) | Done |
| Several pattern and sound slots | Next |
| Digitakt clock sync | Planned |
| Randomise and Evolve | Planned |

The full plan is in [docs/SPEC.md](docs/SPEC.md), and the controls in [docs/CONTROLS.md](docs/CONTROLS.md).

## Working on it

1. On this page: **Code > Codespaces > Create codespace on main**. Setup installs everything (a few minutes the first time).
2. In its terminal:

| Command | What it does |
| --- | --- |
| `./dev.sh test` | All tests, no hardware needed |
| `./dev.sh emu` | The emulator from your own changes, in your browser |
| `./dev.sh build` | The real firmware file: `build/amb.fwsc` |
| `./dev.sh check` | Build plus tests. Run this before anything goes on the FM1. |

GitHub runs the tests on every push (the **Actions** tab), and updates the browser link above from `main`. Pushing a tag such as `v0.1` builds the firmware and publishes it under **Releases**.

## Installing on the FM1

From your PC, with the FM1 connected by USB:

```
pip install mido python-rtmidi
python tools/fm1_install.py --info
python tools/fm1_install.py amb.fwsc
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
| `firmware/src/dsp/amb.c` | The sound engine: chords, filter, echo, reverb, sequencer |
| `firmware/src/app/` | The app on the device: controls, screen, storage |
| `firmware/hal/`, `firmware/loader/` | Hardware and update loader. Don't change. |
| `host/` | Simulator that runs the app on a computer from a script |
| `web/emu/` | Browser emulator |
| `tests/` | Tests and scenario scripts |
| `docs/` | Spec, controls, and DUB 1's original manual and README |

## Credits

dubengine is built on **DUB 1** by sbegueria ([source](https://github.com/sbegueria/fm1-dub)), a fork of Charles Vestal's AMB 1. Its platform layer (hardware, USB, update loader, storage, installer) comes from Leo Kuroshita's **Felucca**. The full history is kept in this repo. DUB 1's generative parts are still in the code, switched off, to come back as options.

Licence: GPL 3.0 only, as upstream. See [LICENSING.md](LICENSING.md).
