---
name: image-compare
description: Comparing rendered screenshots — between thirdeye and DOSBox-captured references, between two thirdeye renders for regression, or between a decoder's output and a ground-truth reference. Covers `odiff` / `spaceman-diff` / PIL+NumPy pixel scans, when to reach for each, and the load-bearing trap of comparing a native 320x200 render against an upscaled-and-blurred DOSBox capture. Use whenever you need to answer "does this look right?" with more rigour than eyeballing. Triggers include "image diff", "screenshot compare", "odiff", "spaceman-diff", "alignment off", "doesn't look right", "dosbox screenshot", "regress this frame", "find where it differs", "is this pixel-perfect".
---

# Comparing screenshots without lying to yourself

Eyeballing a side-by-side stops working the moment the two images are at
different scales, were captured through different rendering pipelines, or
sample colours differently. This skill exists because we burnt a session
"aligning" portraits against a downscaled DOSBox capture — the LANCZOS
resize was shifting frame edges by 2 px and producing a misleading diff.
The fix was to stop trusting the capture and trust the source asset.

## The decision in one paragraph

If you have the **source asset** (a CPS, a BMP, a binary), decode it with a
reference decoder (pure Python is fine) and compare *that* to your engine's
output. Both are native-resolution paletted bitmaps; differences are real
bugs. If you only have a **third-party capture** (DOSBox screenshot, video
frame, web image), the capture has already been through a stretch / filter
/ JPEG pass and is **not** ground truth at the pixel level — use it for
*structural* cross-check only.

## Tools

| Tool | What it does | Use when |
|---|---|---|
| `odiff a.png b.png out.png` | Per-pixel diff with anti-alias tolerance, writes red-overlay PNG | "Did this regress?" against a previously-good render of yours — both at the same native scale |
| `spaceman-diff a.png b.png` | Perceptual diff using terminal output, no PNG needed | Quick "do these look the same to a human?" check; doesn't fight you over sub-pixel anti-aliasing |
| `PIL.ImageChops.difference` | Raw per-pixel subtraction in Python | When you need to *locate* the diff, not score it — feed the result into your own analysis |
| Pure-Python decoder + side-by-side palette dump | Decode the source asset independently of the engine | Ground truth for "is my decoder correct?" — see Methodology below |
| Native-pixel column / row scan | Walk the decoded buffer counting dark pixels per row/col | Find frame positions, edges, alignment offsets in a single rendered image — no second image needed |

All three command-line tools are already installed (`brew install odiff-bin
spaceman-diff`). Reach for them in that order: regression check → human
sanity check → manual investigation.

## The DOSBox-screenshot trap (READ THIS)

A DOSBox screenshot of a 320×200 DOS game is **not** a pixel-accurate
record of what the game drew. By the time the PNG lands on disk it has
passed through:

1. DOSBox's output scaler (often 2x / 3x with nearest, but sometimes a
   blurring filter like `super2xsai`, depending on user config).
2. DOSBox's 4:3 aspect-ratio stretch (320×200 is NOT square pixels —
   stretching them produces a horizontal expansion that doesn't divide
   evenly into the captured frame width).
3. The screenshot encoder.

Effect: a 320×200 image captured by DOSBox at "4x" comes out as something
like **1278×800 or 1288×812** — NOT a clean integer multiple. When you
downscale that back to 320×200 with LANCZOS for diffing, frame edges
shift by 1–2 px due to resampling, and `odiff` lights up 40%+ of the
pixels even when both renders are byte-identical.

**Concrete data from EOB3 chargen alignment:**

- My native CHARGEN.CPS render: slot 1 inner frame at native x=16, x=48
- DOSBox screenshot, LANCZOS-downscaled to 320×200: same frames at x=18, x=50
- DOSBox screenshot, raw: those frames at dosbox x=73, x=201

A naïve diff put the slot at "x=18" because that's where the downscaled
DOSBox said it was. Wrong answer; cost was a session of mis-positioning
portraits.

**Rule:** never let a DOSBox capture's pixel positions overrule a native-
resolution decode of the source asset.

## Methodology: ground truth via independent decoder

The strongest verification is to decode the source asset with a
*different* implementation and compare pixel-for-pixel against the engine.

```python
# Example: verify CHARGEN.CPS C++ decoder against pure-Python LCW
data = open('CHARGEN.CPS', 'rb').read()
file_size, comp, uncomp = struct.unpack('<HHI', data[:8])
pal_sz, = struct.unpack('<H', data[8:10])
src = data[10 + pal_sz:]
pure_pixels = lcw_decompress(src, uncomp)  # 64000 bytes

# Apply palette
pal = open('PALETTE.COL', 'rb').read()
flat = [b << 2 for b in pal]  # 6-bit VGA -> 8-bit
img = Image.new('P', (320, 200))
img.putpalette(flat)
img.frombytes(pure_pixels)
img.convert('RGB').save('/tmp/pure.png')

# Compare to the engine's own dump (must be at native 320x200 too)
engine = Image.open('/tmp/engine.bmp').convert('RGB')
pure_rgb = img.convert('RGB')
# row-by-row comparison — exact equality if both decoders are correct
for y in range(200):
    for x in range(320):
        if engine.getpixel((x,y)) != pure_rgb.getpixel((x,y)):
            print(f'diff at ({x},{y})')
```

If this returns no diffs, your engine's decoder is correct. Any
positional disagreement with a DOSBox capture from this point is the
capture's fault, not yours.

## Finding alignment via native-pixel scans

Often you have ONE image (your engine's render) and need to find where
the frames / slots / text boxes are. No comparison needed — scan for
structural pixels directly:

```python
# Find vertical frame lines: columns that are dark across many rows
from PIL import Image
im = Image.open('/tmp/backdrop.bmp').convert('RGB')
counts = {}
for x in range(im.width):
    counts[x] = sum(1 for y in range(y_min, y_max)
                    if max(im.getpixel((x, y))) < 30)
threshold = (y_max - y_min) * 0.5
for x, c in sorted(counts.items()):
    if c > threshold:
        print(f'x={x}: dark in {c}/{y_max-y_min} rows  ← frame line')

# Same for horizontal frames:
for y in range(im.height):
    dark = sum(1 for x in range(x_min, x_max)
               if max(im.getpixel((x, y))) < 30)
    if dark > (x_max - x_min) * 0.5:
        print(f'y={y}: dark in {dark}/{x_max-x_min} cols  ← frame line')
```

This is how we found the EOB3 chargen slot positions: solid frames
showed up as 32/32 dark across the slot's x range, junk stone cracks
showed up as 15/32 (and got ignored).

## Visual debugging: overlay test rectangles

When you're tuning a position, draw your candidate rect on the
backdrop and look at it upscaled NEAREST:

```python
from PIL import ImageDraw
im = Image.open('/tmp/backdrop.bmp').convert('RGB')
draw = ImageDraw.Draw(im)
for (x, y, w, h), color in zip(rects, ['red', 'green', 'blue', 'yellow']):
    draw.rectangle([x, y, x+w-1, y+h-1], outline=color)
im.resize((1280, 800), Image.NEAREST).save('/tmp/overlay.png')
```

NEAREST upscale (not LANCZOS, not BILINEAR) so individual pixels stay
sharp and you can count them. The 4x scale (320→1280) makes pixel
positions readable while keeping the file small.

## Diff-tool examples

```bash
# Regression: did this change break the title screen?
odiff /tmp/before.png /tmp/after.png /tmp/diff.png
# Returns "Found N different pixels (P%)" — high P means visible regression.

# Sanity check: do these two renders look the same to a human?
spaceman-diff render_a.png render_b.png
# Outputs a perceptual delta; low value = visually equivalent.

# Locate the diff: where exactly does the engine disagree with ground truth?
python3 -c "
from PIL import Image, ImageChops
diff = ImageChops.difference(Image.open('a.png'), Image.open('b.png'))
bbox = diff.getbbox()
print('Differences in region:', bbox)
diff.save('/tmp/where.png')
"
```

## When NOT to use any of these

- "Does this code compile?" → not an image problem; use the build.
- "Did Bob's stats roll correctly?" → game-state question, read the
  party data, not the screenshot.
- "Why does the cursor blink wrong?" → a single snapshot won't show
  animation; record a sequence and diff frame-pairs.

## Quick triage when something "looks off"

1. Is there a source asset you can decode independently? → Do that first.
   Compare engine-render vs independent-decode at native scale.
2. Is the only reference a third-party capture? → Use it for *structural*
   confirmation only (are slots in a 2×2 grid? does the title text exist?),
   never for pixel-precise alignment.
3. Found a real diff? → Native-pixel scan to *locate* it, then trace
   back to the decode/blit code. Don't try to "tune" the position to
   match a capture that's been through interpolation.
