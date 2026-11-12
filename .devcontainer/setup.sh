#!/bin/sh
# One time Codespace setup: system libraries, Python packages, the JieLi toolchain,
# the JieLi AC79 SDK and Emscripten (for the browser emulator).
# Each step is independent: if one fails, the rest still run, and the message says how to retry.
set -u
cd "$(dirname "$0")/.."

echo "== system libraries (font rendering needs libraqm and fribidi)"
sudo apt-get update -qq && sudo apt-get install -y -qq libfribidi0 libraqm0 xz-utils >/dev/null \
    || echo "setup: apt step failed (rerun: sh .devcontainer/setup.sh)"

echo "== python packages"
pip install --quiet pillow || echo "setup: pip install pillow failed"

echo "== JieLi toolchain (clang 4.0.1 for pi32v2)"
if [ -x "$HOME/.jieli/toolchain/pi32v2/bin/clang" ]; then
    echo "already installed"
else
    sh tools/get_toolchain.sh || echo "setup: toolchain download failed (rerun: sh tools/get_toolchain.sh)"
fi

echo "== JieLi AC79 SDK (the build uses three of its files)"
SDK="$HOME/fw-AC79_AIoT_SDK"
if [ -f "$SDK/cpu/wl82/tools/uboot.boot" ]; then
    echo "already installed"
else
    git clone --depth 1 --branch AC79NN_SDK_V1.2.1_2023-12-13 \
        https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK.git "$SDK" \
        || echo "setup: SDK clone failed (rerun: sh .devcontainer/setup.sh)"
fi

echo "== Emscripten (browser emulator)"
if [ ! -d "$HOME/emsdk" ]; then
    git clone --depth 1 https://github.com/emscripten-core/emsdk.git "$HOME/emsdk" \
        && "$HOME/emsdk/emsdk" install latest >/dev/null \
        && "$HOME/emsdk/emsdk" activate latest >/dev/null \
        || echo "setup: Emscripten install failed (the emulator won't build, everything else will)"
fi
grep -q emsdk_env "$HOME/.bashrc" 2>/dev/null \
    || echo '[ -f "$HOME/emsdk/emsdk_env.sh" ] && . "$HOME/emsdk/emsdk_env.sh" >/dev/null 2>&1' >> "$HOME/.bashrc"

echo "== done. Open a new terminal, then: ./dev.sh test"
