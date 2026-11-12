#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# The stems (host/stems.c): tests/host/stems.amb without them and with every one (raw, AMB_STEMS_FF); the film and
# the WAV the same, byte for byte; each stem the film's frame count, 240 x 135 RGB565 a frame. Needs ffmpeg.
#   tests/host/stems_test.sh        (after host/build_host.sh)
set -e
cd "$(dirname "$0")/../.."
D=build/host/stems_test
rm -rf "$D"
mkdir -p "$D/a" "$D/b/stems"
build/host/amb_host tests/host/stems.amb "$D/a" > "$D/a.log"
AMB_STEMS="$D/b/stems" AMB_STEMS_FF="-f rawvideo" build/host/amb_host tests/host/stems.amb "$D/b" > "$D/b.log"
cmp "$D/a/stems.film" "$D/b/stems.film"
cmp "$D/a/stems.wav" "$D/b/stems.wav"
F=$(( $(wc -c < "$D/a/stems.film") / (13 * 4 + 240 * 240 * 2) ))
n=0
for s in "$D"/b/stems/*.mp4; do
    [ $(( $(wc -c < "$s") )) -eq $(( F * 240 * 135 * 2 )) ] || { echo "$s: not $F frames"; exit 1; }
    n=$((n + 1))
done
[ $n -eq 21 ] || { echo "$n stems, not 21"; exit 1; }
echo "stems: the film and the WAV the same with $n stems as without; each $F frames"
rm -rf "$D/b/stems"
