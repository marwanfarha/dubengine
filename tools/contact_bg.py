# SPDX-License-Identifier: GPL-3.0-only
# Contact sheets of the Winter Set hour's background composite (DUB-1-set2-1h-bg.mp4): at each change of
# background (the song's downbeat from the cue sheet), six frames from 3 s before to 3 s after; and one frame from
# each song. Each frame 640x360 with its time under it.
#   contact_bg.py VIDEO CUES OUTDIR [SONG_TIMES (s, comma-separated)]
import re, subprocess, sys
from PIL import Image, ImageDraw, ImageFont
video, cues, outd = sys.argv[1:4]
F = ImageFont.truetype("assets/fonts/BarlowSemiCondensed-SemiBold.ttf", 24)
S = []
for line in open(cues, encoding="utf-8"):
    m = re.match(r"@ (\d+):(\d+\.\d)\s+bar \d+\s+(\d) ([A-Z][A-Z ]+?):", line)
    if m:
        S.append((max(int(m[1]) * 60 + float(m[2]) - 0.6, 0.0), int(m[3]), m[4].title()))   # (the downbeat: 0.6 s before its line)

def grab(t):
    r = subprocess.run(["nice", "-n", "10", "ffmpeg", "-v", "error", "-ss", "%.3f" % t, "-i", video, "-frames:v", "1",
                        "-vf", "scale=640:360:flags=area", "-f", "rawvideo", "-pix_fmt", "rgb24", "-"], capture_output=True).stdout
    return Image.frombytes("RGB", (640, 360), r)

def sheet(items, cols, title, path):
    rows = (len(items) + cols - 1) // cols
    W, H = 640 * cols + 8 * (cols + 1), 56 + (360 + 40) * rows + 8
    im = Image.new("RGB", (W, H), (20, 22, 26))
    d = ImageDraw.Draw(im)
    d.text((12, 14), title, font=F, fill=(230, 234, 238))
    for i, (t, label) in enumerate(items):
        x, y = 8 + (640 + 8) * (i % cols), 56 + (360 + 40) * (i // cols)
        im.paste(grab(t), (x, y))
        d.text((x + 4, y + 364), "%d:%05.2f  %s" % (int(t) // 60, t % 60, label), font=F, fill=(176, 196, 216))
    im.save(path, quality=90)
    print(path)

for k in range(1, len(S)):
    D, n, name = S[k]
    prev = S[k - 1][2]
    items = [(D + o, l) for o, l in [(-3, "3 s before: %s" % prev), (-1.5, "crossfade under way"), (-0.5, "half a second before"),
                                     (0.5, "the downbeat + 0.5 s"), (1.5, "crossfade ending"), (3, "3 s after: %s" % name)]]
    sheet(items, 3, "Change %d: %s -> %s (the downbeat %d:%04.1f; the crossfade 4 s centred on it)" % (k, prev, name, int(D) // 60, D % 60),
          "%s/contact-change%d-%s.jpg" % (outd, k, name.lower().replace(" ", "")))
T = [float(x) for x in sys.argv[4].split(",")] if len(sys.argv) > 4 else [S[i][0] + 300 for i in range(len(S))]
sheet([(t, "%d %s" % (S[i][1], S[i][2])) for i, t in enumerate(T)], 3, "One frame from each song", "%s/contact-songs.jpg" % outd)
