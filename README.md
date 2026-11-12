# dubengine

Custom firmware for the **M VAVE FM1** that turns it into a chord sequencer for dub techno, minimal and microhouse.

**[▶ Play it in your browser](https://marwanfarha.github.io/dubengine/)**, the same code the FM1 runs. Turn your phone sideways for bigger controls.

## The idea

Dub techno lives on chord stabs: one shape moved around a key, thrown into a tape echo and a big reverb. dubengine makes the FM1 do exactly that, and play alongside an Elektron Digitakt II.

1. **Pick a key.** Any root note, any mode. Only the keys in your scale light up.
2. **Play chords with one finger.** Each lit key plays the triad your scale builds on it. Root position, 1st or 2nd inversion.
3. **Record a pattern.** Turn the metronome on, set the length, and record live. Or stop and enter chords step by step.
4. **Shape each step.** Lock the chord, voicing, octave and length of any step, like Elektron parameter locks.
5. **Let it move.** Randomise writes a new pattern in your key. Density, Variation, Length and Offbeat shape it, and Evolve lets it change by itself as it loops.
6. **Sync it.** It runs on its own tempo, or follows the Digitakt's clock when they're connected.

Every chord comes from its own synth: two oscillators a voice (sine, triangle, saw or square), a wavefolder, a resonant filter, amp and filter envelopes, two more envelopes and two LFOs to modulate almost anything. Then a tape echo and a plate reverb.

## Where it is now

| Feature | Status |
| --- | --- |
| Keys play chords in the chosen key, held while pressed | Done |
| Saves work on both FM1 flash chips | Done |
| Metronome, pattern length, step value, live recording | Done |
| Step edit, step locks for chord, voicing, octave, length | Done |
| Synth: 4 waves, fold, filter, amp and filter envelopes, 2 mod envs, 2 LFOs | Done |
| Delay page (time, feedback, mix, tone, wow, drive) and two Reverb pages (decay, size, pre-delay, mix, width, high cut, damping, diffusion, modulation, low cut, freeze) | Done |
| Saving patterns and sounds | Next |
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
