# dubengine

Custom firmware for the M Vave FM1, aimed at dub techno, minimal and atmospheric work alongside an Elektron Digitakt II.

It starts from **DUB 1** by sbegueria ([source](https://github.com/sbegueria/fm1-dub)), with its full history kept. DUB 1 is a fork of Charles Vestal's AMB 1, and its platform layer (hardware, USB, update loader, storage, installer) comes from Leo Kuroshita's Felucca. The original README is in [docs/DUB1_README.md](docs/DUB1_README.md).

Licence: GPL 3.0 only, as upstream. See [LICENSING.md](LICENSING.md).

## Working in the Codespace

1. On this page: **Code > Codespaces > Create codespace on main**.
2. Wait for setup to finish (a few minutes the first time). It installs the JieLi toolchain, the AC79 SDK and Emscripten.
3. Open a new terminal and use:

| Command | What it does |
| --- | --- |
| `./dev.sh test` | Host tests and simulator scenarios. No hardware needed. |
| `./dev.sh emu` | Builds the browser emulator and opens it. Play it with your mouse or a MIDI keyboard. |
| `./dev.sh build` | Builds the real firmware: `build/amb.fwsc` |
| `./dev.sh check` | Build plus all tests. Run this before anything goes on the FM1. |

GitHub also runs the tests on every push (the **Actions** tab), and keeps the emulator and scenario audio as downloads.

## Releasing

Push a tag such as `v0.1`. GitHub builds the `.fwsc` and publishes it under **Releases**.

## Installing on the FM1

From your PC (Chrome or Edge can't reach USB from inside a Codespace):

```
pip install mido python-rtmidi
python tools/fm1_install.py --info
python tools/fm1_install.py amb.fwsc
```

## Safety rules

1. **Test first.** Every change goes through `./dev.sh check` and the emulator before it goes near the hardware.
2. **Keep the stock firmware.** Download FM1 V15 ("PC Firmware") from m-vave.com/download and keep it.
3. **Never touch the bootloader or the update loader** (`firmware/loader/`, the package layout in `tools/fm1pkg_make.py`). We only change the app.
4. **Flash chip.** This code only saves on the original flash chip (`0x856014`, see `firmware/src/app/main_fm1.c`). Newer FM1s with chip `0xB36014` run but can't save. Supporting both chips is our first job.
5. **Recovery, in order:**
   * Won't start: hold OCT minus while switching on, then install V15 or any firmware over USB.
   * PC only shows a USB disk "WL82 UBOOT1.00": the ROM bootloader can't be erased by an update. Use [M Flasher](https://github.com/jvitkauskas/m-flasher) or [MvaveFM1Unbricker](https://github.com/Quixotic7/MvaveFM1Unbricker).
   * Never shows that disk: [FM-1 Transporter](https://github.com/kurogedelic/FM-1-transporter).
   * Full guide: [Firmware Hub rescue](https://thefirmwarehub.com/tools/rescue/).

## Where things are

| Path | What |
| --- | --- |
| `firmware/src/dsp/amb.c` | The sound engine: chords, voicing, echo, reverb, generative parts |
| `firmware/src/app/` | The app on the device: UI, controls, storage |
| `firmware/hal/`, `firmware/loader/` | Hardware and update loader. Don't change. |
| `host/` | Simulator that runs the app on a computer from a script |
| `web/emu/` | Browser emulator |
| `tests/` | Host tests and scenario scripts |
| `docs/MANUAL.md` | DUB 1's manual |
| `PLAN.md` | DUB 1's design notes |

## References

* [Felucca BUILDING.md](https://github.com/hugelton/Felucca/blob/main/BUILDING.md)
* [fm1 build](https://github.com/zvenson/fm1-build)
* [Firmware Hub developer docs](https://thefirmwarehub.com/kb/)
* [FM 1 RE](https://github.com/AL-255/FM-1-RE): architecture and memory map
* [Awesome FM 1](https://github.com/cicloid/awesome-fm-1)

## Staying in sync with DUB 1

```
git remote add upstream https://github.com/sbegueria/fm1-dub
git fetch upstream
git merge upstream/main
```
