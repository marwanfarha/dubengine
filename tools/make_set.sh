#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# "Winter Set": the second hour-long listening video (tests/film/21-set.amb, five songs in Set mode, Transition Mix),
# 1920x1080, the screen alone (tools/film.py --layout center), in build/film/channel/long_videos/set2/. As
# tools/make_night.sh: an hour of film never goes to disk; the simulator is deterministic, so it runs twice.
#   1. the film to /dev/null: the WAV, the cue sheet and the song log; each song's loudness measured, its gain set to
#      bring it near TARGET (-16 LUFS), ramped over 8 s from each downbeat; fades, a true-peak limiter; the levels
#      after, the limiter's work, the longest quiet stretch and the tempo across each Mix measured (levels.txt);
#      the captions and the chapters from the cues
#   2. the film into a named pipe, tools/film.py reading it as it comes and encoding the picture alone; its WAV
#      compared with the first (the two runs the same, sample for sample); the mastered audio muxed in; a thumbnail
#   3. (STEMS) the stems: the film again, to /dev/null, each of the screen's pictures alone a video of its own
#      (host/stems.c: 1920x1080, frame for frame with the film, to lay under it in the edit) in $OUT/stems/; its WAV
#      and its cues compared with the first's
#   tools/make_set.sh            (after host/build_host.sh; WORK=dir for the temporary files)
#   SCRIPT=file OUT=dir          the same pipeline on a short test take (its wav and film named as here)
#   GAINS="g1 .. g5"             the songs' gains (dB) by hand, in place of the measured ones
#   PASS1=1                      stop after the master (no picture)
#   STEMS=1 | STEMS=name,name    pass 3 after the rest: every stem, or those named (host/stems.c's list)
#   STEMS_ONLY=1                 the stems without pass 2 (the picture, the mux, the thumbnail)
set -e
cd "$(dirname "$0")/.."
PY="${PYTHON:-.venv/bin/python3}"
N=21-set
SCRIPT="${SCRIPT:-tests/film/$N.amb}"
OUT="${OUT:-build/film/channel/long_videos/set2}"
WORK="${WORK:-build/film/set2-work}"
TARGET="${TARGET:--16}"
DIP="${DIP:-5}"                  # (dB: the held echo at each change ridden down)
# the limiter's ceiling (linear; 0.84, -1.5 dBTP), oversampled at 192 kHz as the night's
LIMIT="${LIMIT:-0.84}"
B=DUB-1-set2-1h
mkdir -p "$OUT" "$WORK/p1" "$WORK/p2"

echo "== pass 1: the audio, the cue sheet, the song log"
rm -f "$WORK/p1/$N.film"
ln -sf /dev/null "$WORK/p1/$N.film"
build/host/amb_host "$SCRIPT" "$WORK/p1" > "$WORK/cues.log" || true
if grep -q FAIL "$WORK/cues.log" || ! grep -q '^host:' "$WORK/cues.log"; then
    grep -E 'FAIL|^line' "$WORK/cues.log" || tail -3 "$WORK/cues.log"
    exit 1
fi
grep '^@' "$WORK/cues.log" > "$OUT/cues.txt"

# the songs (the cues "N NAME: ..." at each downbeat, 0.6 s after it) and the end ("THE END:"): each song's raw
# loudness, its gain, the envelope; the captions and the chapters
"$PY" - "$OUT/cues.txt" "$WORK" "$WORK/p1/$N.wav" "$OUT/chapters.txt" "$TARGET" "$DIP" $GAINS <<'EOF'
import re, subprocess, sys
cues, work, wav, chap, target, DIP = sys.argv[1:7]
target, DIP = float(target), float(DIP)
hand = [float(g) for g in sys.argv[7:]]
S, end = [], None
for line in open(cues, encoding="utf-8"):
    m = re.match(r"@ (\d+):(\d+\.\d)\s+bar \d+\s+(\d) ([A-Z][A-Z ]+?):", line)
    t = lambda: int(m[1]) * 60 + float(m[2])
    if m:
        S.append([max(t() - 0.6, 0.0), int(m[3]), m[4].strip().title()])
    m = re.match(r"@ (\d+):(\d+\.\d)\s+bar \d+\s+THE END:", line)
    if m:
        end = t()
assert S and end, (S, end)
S[0][0] = 0.0
def lufs(a, b):
    o = subprocess.run(["ffmpeg", "-nostats", "-ss", "%.2f" % a, "-to", "%.2f" % b, "-i", wav, "-af", "ebur128=peak=true",
                        "-f", "null", "-"], capture_output=True, text=True).stderr
    o = o[o.rindex("Summary:"):]
    return float(re.search(r"I:\s+(-?[\d.]+) LUFS", o)[1]), float(re.search(r"Peak:\s+(-?[\d.]+) dBFS", o)[1])
G, rows = [], []
for i, (t, k, name) in enumerate(S):
    b = S[i + 1][0] if i + 1 < len(S) else end
    I, pk = lufs(t, b)
    g = hand[i] if i < len(hand) else round(target - I, 1)
    G.append(g)
    rows.append("%d %-10s %7.1f-%7.1f s  raw %6.1f LUFS  peak %5.1f dBTP  gain %+5.1f dB" % (k, name, t, b, I, pk, g))
open(work + "/songs.txt", "w").write("\n".join(rows) + "\n")
print("\n".join(rows))
with open(work + "/captions.tsv", "w", encoding="utf-8") as f:   # (each name a few seconds in: past the launch
    for i, (t, k, name) in enumerate(S):                           # screen; past the held echo's bar)
        f.write("%.1f\t%d of %d \u00b7 %s\n" % (3.0 if i == 0 else t + 4.5, k, 5, name))
with open(chap, "w", encoding="utf-8") as f:
    for t, k, name in S:
        s = int(t)
        f.write("%d:%02d %d \u00b7 %s\n" % (s // 60, s % 60, k, name))
X, R = [s[0] for s in S[1:]], 4.0
def env(i):
    if i == len(X):
        return "%.2f" % G[i]
    a, b = X[i] + 0.5, X[i] + 0.5 + 2 * R          # (after the downbeat: the held echo keeps the old song's gain)
    return "if(lt(t,%.2f),%.2f,if(lt(t,%.2f),%.2f+(%.2f)*(t-%.2f)/%.1f,%s))" % (a, G[i], b, G[i], G[i + 1] - G[i], a, 2 * R, env(i + 1))
# the Mix's held echo (pushed +6 dB, from 2 bars before each downbeat into the next song's first bar) stands about 5 dB over
# the music: ridden down DIP dB as a DJ's fader would, from 7.5 s before the downbeat to 3 s after (the thrown stabs and the hold), over 1.5 s each way
dip = "".join("-%.1f*max(0,min(min(1,(t-%.2f)/1.5),(%.2f-t)/1.5))" % (DIP, x - 7.5, x + 3.0) for x in X)
open(work + "/gain.txt", "w").write("pow(10,(%s%s)/20)\n%.1f\n%s\n" % (env(0), dip, end, " ".join("%.2f" % s[0] for s in S)))
EOF
cat "$OUT/chapters.txt"
E=$(sed -n 1p "$WORK/gain.txt")
FADE=$(sed -n 2p "$WORK/gain.txt")
echo "== mastering (fade-in 10 s, fade-out 20 s from $FADE s, the songs' gains, the limiter at $LIMIT)"
ffmpeg -nostats -loglevel error -y -i "$WORK/p1/$N.wav" -af "volume='$E':eval=frame,afade=t=in:st=0:d=10,afade=t=out:st=$FADE:d=20,aresample=48000" \
    -c:a pcm_f32le "$WORK/gained.wav"
ffmpeg -nostats -loglevel error -y -i "$WORK/gained.wav" -af "aresample=192000,alimiter=limit=$LIMIT:attack=5:release=300:level=disabled,aresample=48000" \
    -c:a pcm_s24le "$OUT/$B-master.wav"
ffmpeg -nostats -i "$OUT/$B-master.wav" -af ebur128=peak=true -f null - 2>&1 | grep -A14 Summary | grep -E ' I:| LRA:| Peak:'

echo "== the measurements (levels.txt)"
"$PY" - "$WORK" "$OUT/$B-master.wav" "$WORK/p1/$N-tlog.txt" "$OUT/levels.txt" <<'EOF'
import re, subprocess, sys, wave
import numpy as np
work, master, tlog, outf = sys.argv[1:5]
lines = open(work + "/gain.txt").read().split("\n")
end = float(lines[1])
X = [float(x) for x in lines[2].split()]
songs = open(work + "/songs.txt").read().strip().split("\n")
out = ["Raw, then mastered (each song from its downbeat to the next; the fades in the first and the last):"]
def lufs(a, b):
    o = subprocess.run(["ffmpeg", "-nostats", "-ss", "%.2f" % a, "-to", "%.2f" % b, "-i", master, "-af", "ebur128=peak=true",
                        "-f", "null", "-"], capture_output=True, text=True).stderr
    o = o[o.rindex("Summary:"):]
    return float(re.search(r"I:\s+(-?[\d.]+) LUFS", o)[1]), float(re.search(r"Peak:\s+(-?[\d.]+) dBFS", o)[1])
for i, row in enumerate(songs):
    a = X[i]
    b = X[i + 1] if i + 1 < len(X) else end
    I, pk = lufs(a, b)
    out.append("  %s  ->  %6.1f LUFS, peak %5.1f dBTP" % (row, I, pk))
def read(p):
    w = subprocess.run(["ffmpeg", "-nostats", "-loglevel", "error", "-i", p, "-f", "f32le", "-ac", "2", "-ar", "48000", "-"],
                       capture_output=True).stdout
    return np.frombuffer(w, dtype=np.float32).reshape(-1, 2)
pre, post = read(work + "/gained.wav"), read(master)[240:]   # (the limiter's 5 ms look-ahead)
n = min(len(pre), len(post))
blk = 4800                                   # 100 ms
k = n // blk
def blocks(a, f):
    a = a[:k * blk].reshape(k, blk, 2)
    return f(a)
pk_pre = blocks(pre, lambda a: np.abs(a).max(axis=(1, 2)))
pk_post = blocks(post, lambda a: np.abs(a).max(axis=(1, 2)))
gr = 20 * np.log10(np.maximum(pk_pre, 1e-9) / np.maximum(pk_post, 1e-9))
loud = pk_pre > 10 ** (-40 / 20)
out.append("The limiter (100 ms blocks, its gain reduction at the block's peak): over 0.5 dB in %.2f%%, over 1 dB in %.2f%%, "
           "over 2 dB in %.3f%%, over 3 dB in %.3f%% of the blocks; the most %.1f dB" %
           (100 * np.mean(gr > 0.5), 100 * np.mean(gr > 1), 100 * np.mean(gr > 2), 100 * np.mean(gr > 3), gr[loud].max()))
for i in range(len(X)):
    a = int(X[i] * 10)
    b = int((X[i + 1] if i + 1 < len(X) else end) * 10)
    g = gr[a:b]
    out.append("  song %d: over 1 dB in %.2f%%, over 2 dB in %.3f%%, the most %.1f dB" % (i + 1, 100 * np.mean(g > 1), 100 * np.mean(g > 2), g.max()))
w = 2400                                     # 50 ms RMS, the master, from 1 s in to the fade-out
m = int(end * 48000) // w
r = post[:m * w].reshape(m, w, 2)
db = 10 * np.log10(np.mean(r.astype(np.float64) ** 2, axis=(1, 2)) + 1e-20)
q = db < -60
run, best, at = 0, 0, 0
for j in range(20, m):
    run = run + 1 if q[j] else 0
    if run > best:
        best, at = run, j - run + 1
out.append("The longest stretch under -60 dBFS (50 ms RMS) from 1 s to the fade-out: %.2f s (at %.1f s)" % (best * w / 48000, at * w / 48000))
clip = int(np.sum(np.abs(post) >= 0.999))
out.append("Samples at full scale in the master: %d" % clip)
try:
    T = np.loadtxt(tlog, usecols=(1, 2, 3, 4, 12))   # wav s, the tempo heard, the song's place, bars, the piece
    out.append("The tempo across each Mix (the last 10 bars of each song, then the next's first 2, bar by bar):")
    for i in range(1, len(X)):
        sel = (T[:, 0] > X[i] - 21.0) & (T[:, 0] < X[i] + 4.5)
        seg = T[sel]
        bars = []
        for t0 in np.arange(X[i] - 20.0, X[i] + 4.0, 2.0):
            j = np.argmin(np.abs(seg[:, 0] - t0))
            bars.append("%.1f" % seg[j, 1])
        out.append("  into song %d: %s" % (i + 1, " ".join(bars)))
except Exception as e:
    out.append("(no song log: %s)" % e)
open(outf, "w").write("\n".join(out) + "\n")
print("\n".join(out))
EOF
[ -n "$PASS1" ] && exit 0

stems() {
    echo "== pass 3: the stems (${STEMS:-all}), into $OUT/stems"
    mkdir -p "$OUT/stems" "$WORK/p3"
    ln -sf /dev/null "$WORK/p3/$N.film"
    S=$STEMS
    [ "$S" = 1 ] || [ -z "$S" ] && S=all
    AMB_STEMS="$OUT/stems $S" build/host/amb_host "$SCRIPT" "$WORK/p3" > "$WORK/cues3.log" || true
    cmp "$WORK/p1/$N.wav" "$WORK/p3/$N.wav"
    grep '^@' "$WORK/cues3.log" | cmp - "$OUT/cues.txt"
    echo "  the same run, sample for sample"
    ls -la "$OUT/stems"
}
[ -n "$STEMS_ONLY" ] && { stems; exit 0; }

echo "== pass 2: the picture, through a pipe"
rm -f "$WORK/p2/$N.film"
mkfifo "$WORK/p2/$N.film"
"$PY" tools/film.py "$WORK/p2/$N.film" - "$WORK/video.mp4" --layout center --captions "$WORK/captions.tsv" \
    --crf 16 --preset slow &
FP=$!
build/host/amb_host "$SCRIPT" "$WORK/p2" > "$WORK/cues2.log" || true
wait $FP
rm -f "$WORK/p2/$N.film"
cmp "$WORK/p1/$N.wav" "$WORK/p2/$N.wav"
grep '^@' "$WORK/cues2.log" | cmp - "$OUT/cues.txt"
echo "  the two runs are the same, sample for sample"

echo "== mux"
ffmpeg -nostats -loglevel error -y -i "$WORK/video.mp4" -i "$OUT/$B-master.wav" -map 0:v -map 1:a \
    -c:v copy -c:a aac -b:a 320k -movflags +faststart "$OUT/$B.mp4"

echo "== thumbnail"
TT="${THUMB_T:-$(sed -n 3p "$WORK/gain.txt" | awk '{print ($4 ? $4 + 700 : 60)}')}"   # (the tape, in Aurora)
ffmpeg -nostats -loglevel error -y -ss "$TT" -i "$WORK/video.mp4" -frames:v 1 "$WORK/thumb-frame.png"
"$PY" - "$WORK/thumb-frame.png" "$OUT/set2-thumb.jpg" <<'EOF'
import sys
from PIL import Image, ImageDraw, ImageFilter, ImageFont
src, dst = sys.argv[1:3]
F = "assets/fonts/BarlowSemiCondensed-"
W, H = 1280, 720
bg = Image.new("RGB", (W, H))
d = ImageDraw.Draw(bg)
for y in range(H):                               # a cold night going blue to the first light
    t = y / H
    d.line([(0, y), (W, y)], fill=(int(12 + 18 * t), int(16 + 30 * t), int(26 + 46 * t)))
glow = Image.new("RGB", (W, H), (0, 0, 0))
ImageDraw.Draw(glow).ellipse([700, 40, 1260, 600], fill=(60, 110, 150))
bg = Image.blend(bg, glow.filter(ImageFilter.GaussianBlur(120)), 0.35)
sc = Image.open(src).convert("RGB").crop((480, 60, 1440, 1020)).resize((560, 560), Image.LANCZOS)
mask = Image.new("L", sc.size, 0)
ImageDraw.Draw(mask).rounded_rectangle([0, 0, 559, 559], 28, fill=255)
bg.paste(sc, (660, 80), mask)
d = ImageDraw.Draw(bg)
d.text((60, 96), "WINTER", font=ImageFont.truetype(F + "Bold.ttf", 150), fill=(238, 242, 246))
d.text((64, 262), "A SET IN FIVE SONGS", font=ImageFont.truetype(F + "SemiBold.ttf", 58), fill=(176, 196, 216))
f = ImageFont.truetype(F + "Bold.ttf", 64)
d.rounded_rectangle([56, 440, 300, 522], 10, fill=(92, 196, 204))
d.text((74, 446), "1 HOUR", font=f, fill=(16, 20, 26))
d.text((60, 600), "generative dub techno", font=ImageFont.truetype(F + "SemiBold.ttf", 40), fill=(92, 196, 204))
bg.save(dst, quality=92)
EOF
[ -n "$STEMS" ] && stems
ls -la "$OUT"
