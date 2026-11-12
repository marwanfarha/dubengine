#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# The Winter Set hour with a background (DUB-1-set2-1h-bg.mp4): the screen (960x960 at 480,60 of the main video,
# tools/make_set.sh's pass 2) over one of its stems full-screen, blurred and dimmed, a soft shadow round the screen;
# the stem changing per song, crossfaded over XF s centred on each song's downbeat (the cue sheet's "N NAME:" lines, 0.6 s after it);
# the main video's audio (the master, AAC 320 kb/s) copied. As build/stems/sample/composite.sh, the whole hour.
#   composite-bg.sh MAIN.mp4 STEMS_DIR OUT.mp4 [CUES.txt]
#   BG="ripples scope spectrum tape ripples"   the stems, one per song
#   XF=4                                         the crossfade (s; centred on the downbeat)
#   SIGMA=10 (the blur, at 1080p: done at half size, SIGMA/2), the dim per stem (DIM_ripples=0.8 ..),
#   ZOOM_ripples=1.4 (the ripples' rings mostly sit behind the screen: blown up, more of them show beside it)
#   SHADOW=0.85 SHADOW_R=110 (the shadow: its alpha at the screen's edge, its reach in px)
set -e
MAIN=$1; STEMS=$2; OUT=$3; CUES=${4:-$(dirname "$MAIN")/cues.txt}
[ -f "$MAIN" ] && [ -d "$STEMS" ] && [ -n "$OUT" ] || { sed -n 2,12p "$0"; exit 1; }
PY="${PYTHON:-python3}"
BG="${BG:-ripples scope spectrum tape ripples}"
XF="${XF:-4}"
SIGMA="${SIGMA:-10}"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

# the main video's frames (counted)
NF="${NF:-$(ffmpeg -nostats -i "$MAIN" -map 0:v -c copy -f null - 2>&1 | grep -o "frame= *[0-9]*" | tail -1 | grep -o "[0-9]*")}"
echo "the main video: $NF frames"
# the shadow, and the command (inputs and filter graph) from the downbeats
"$PY" - "$TMP" "$CUES" "$STEMS" "$MAIN" "$OUT" "$BG" "$XF" "$SIGMA" "${SHADOW:-0.85}" "${SHADOW_R:-110}" \
    "${DIM_ripples:-0.8}" "${DIM_scope:-0.65}" "${DIM_spectrum:-0.45}" "${DIM_tape:-0.6}" "${ZOOM_ripples:-1.4}" "$NF" <<'EOF'
import re, shlex, sys
import numpy as np
from PIL import Image
tmp, cues, stems, main, out, bg, xf, sigma, sa, sr, dr, ds, dp, dt, zr, nframes = sys.argv[1:17]
bg, xf, sigma, sa, sr = bg.split(), float(xf), float(sigma), float(sa), float(sr)
DIM = {"ripples": float(dr), "scope": float(ds), "spectrum": float(dp), "tape": float(dt)}
ZOOM = {"ripples": float(zr)}
FPS = 30
LAG = 0.6         # (the cue sheet's song lines come 0.6 s after the downbeat: tools/make_set.sh)
# the shadow: black, alpha sa at the screen's edge falling (smoothstep) to 0 sr px out
y, x = np.mgrid[0:1080, 0:1920].astype(np.float64)
d = np.hypot(np.maximum(np.maximum(480 - x, x - 1439), 0), np.maximum(np.maximum(60 - y, y - 1019), 0))
t = np.clip(1 - d / sr, 0, 1)
img = np.zeros((1080, 1920, 4), np.uint8)
img[..., 3] = np.round(sa * t * t * (3 - 2 * t) * 255).astype(np.uint8)
Image.fromarray(img).save(tmp + "/shadow.png")
# the downbeats (each song's line, after the first's, less its lag: the chapters' times)
D = []
for line in open(cues, encoding="utf-8"):
    m = re.match(r"@ (\d+):(\d+\.\d)\s+bar \d+\s+(\d) [A-Z][A-Z ]+:", line)
    if m and int(m[3]) > 1:
        D.append(int(m[1]) * 60 + float(m[2]) - LAG)
assert len(D) == len(bg) - 1, (D, bg)
# the crossfades' starts (frames), the film's length (frames: the main video's, given)
A = [round((d - xf / 2) * FPS) for d in D]
n = round(xf * FPS)
END = int(nframes)
# the pieces, end to end: each song's stem alone, then the crossfade into the next's (this ffmpeg has no xfade:
# the two blended, the old fading out as the new fades in, linearly over the n frames)
P = []
for k, s in enumerate(bg):
    a = A[k - 1] + n if k else 0
    b = A[k] if k < len(A) else END
    P.append((a, b, [s]))
    if k < len(A):
        P.append((A[k], A[k] + n, [s, bg[k + 1]]))
args, fil, log, ins = ["-i", main, "-loop", "1", "-framerate", str(FPS), "-i", tmp + "/shadow.png"], [], [], 2
def src(s, a, m):
    # a stem from frame a, m frames, zoomed, at half size, blurred and dimmed (its own input: an accurate seek)
    global ins
    args.extend(["-ss", "%.6f" % ((a - 0.25) / FPS), "-t", "%.6f" % ((m + 2) / FPS), "-i", "%s/%s.mp4" % (stems, s)])
    z = ZOOM.get(s, 1.0)
    zf = "crop=iw/%g:ih/%g," % (z, z) if z != 1.0 else ""
    g = DIM[s]
    lab = "s%d" % ins
    fil.append("[%d:v]%sscale=960:540:flags=area,gblur=sigma=%g,colorchannelmixer=rr=%g:gg=%g:bb=%g,format=yuv420p,"
               "setpts=PTS-STARTPTS,trim=end_frame=%d,setpts=PTS-STARTPTS[%s]" % (ins, zf, sigma / 2, g, g, g, m, lab))
    ins += 1
    return lab
outs = []
for i, (a, b, ss) in enumerate(P):
    m = b - a
    if len(ss) == 1:
        outs.append(src(ss[0], a, m))
    else:
        x, y = src(ss[0], a, m), src(ss[1], a, m)
        fil.append("[%s][%s]blend=all_expr='A+(B-A)*min(1,(N+0.5)/%d)'[f%d]" % (x, y, m, i))
        outs.append("f%d" % i)
        log.append("crossfade %-8s -> %-8s frames %d-%d (%s-%s, the downbeat %s)" % (ss[0], ss[1], a, b,
                   "%d:%05.2f" % divmod(a / FPS, 60), "%d:%05.2f" % divmod(b / FPS, 60), "%d:%04.1f" % divmod(D[len(log)], 60)))
fil.append("%sconcat=n=%d:v=1:a=0,scale=1920:1080:flags=bicubic,format=yuv420p[bgc]" % ("".join("[%s]" % o for o in outs), len(outs)))
fil.append("[bgc][1:v]overlay=0:0:shortest=1[bs]")
fil.append("[0:v]crop=960:960:480:60[sc]")
fil.append("[bs][sc]overlay=480:60:shortest=1,format=yuv420p[v]")
open(tmp + "/args", "w").write("\n".join(args) + "\n")
open(tmp + "/graph", "w").write(";\n".join(fil) + "\n")
print("\n".join(log))
EOF
cat "$TMP/graph"
# (the arguments one per line: no spaces in them but the paths', read back as lines)
set --
while IFS= read -r a; do set -- "$@" "$a"; done < "$TMP/args"
ffmpeg -nostats -loglevel error -y "$@" -filter_complex_script "$TMP/graph" -map "[v]" -map 0:a \
    -c:v libx264 -preset "${PRESET:-slow}" -crf "${CRF:-16}" -pix_fmt yuv420p -r 30 -c:a copy -movflags +faststart \
    ${DUR:+-t $DUR} "$OUT"
ls -la "$OUT"
