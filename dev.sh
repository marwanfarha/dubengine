#!/bin/sh
# Shortcuts for day to day work. Run from the repo root.
#   ./dev.sh test      host tests + simulator scenarios (no hardware, no toolchain needed)
#   ./dev.sh emu       build the browser emulator and serve it on port 8000
#   ./dev.sh build     build the real firmware package: build/amb.fwsc
#   ./dev.sh check     test + build, the full check before installing on the FM1
set -e
cd "$(dirname "$0")"
[ -f "$HOME/emsdk/emsdk_env.sh" ] && . "$HOME/emsdk/emsdk_env.sh" >/dev/null 2>&1 || true

case "${1:-}" in
    test)
        sh tests/run_tests.sh
        ;;
    emu)
        sh web/emu/build.sh
        echo "Emulator on port 8000 (Codespaces opens it for you). Ctrl+C to stop."
        cd build/emu && python3 -m http.server 8000
        ;;
    build)
        ./build.sh
        ls -l build/*.fwsc
        ;;
    check)
        ./build.sh
        sh tests/run_tests.sh
        ;;
    *)
        sed -n '2,7p' "$0"
        exit 1
        ;;
esac
