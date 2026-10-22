# Compare the screenshots of all copies per test step (owner 2026-10-22: "every player must see about the same").
# Input: a folder with <step>_<copy>.png (esc4menu.ps1 names). Output per step: <step>_grid.png (2x2 grid, copy
# labels) and one line in shotgrid.txt with each copy's difference from the others (mean absolute difference of
# 96x54 grayscale thumbnails, 0..255). A copy whose difference is far above the others' is flagged ODD.
# Usage: python -I tools/shotgrid.py <folder> [threshold]
import sys, os, re
from PIL import Image, ImageDraw, ImageChops, ImageStat

folder = sys.argv[1]
thr = float(sys.argv[2]) if len(sys.argv) > 2 else 25.0
steps = {}
for f in sorted(os.listdir(folder)):
    m = re.match(r'^(.+)_([A-D])\.png$', f)
    if m and not f.endswith('_grid.png'):
        steps.setdefault(m.group(1), {})[m.group(2)] = os.path.join(folder, f)

def thumb(p):
    return Image.open(p).convert('L').resize((96, 54))

out = []
for step, shots in sorted(steps.items()):
    copies = sorted(shots)
    th = {c: thumb(shots[c]) for c in copies}
    diff = {}
    for c in copies:
        ds = [ImageStat.Stat(ImageChops.difference(th[c], th[o])).mean[0] for o in copies if o != c]
        diff[c] = sum(ds) / len(ds) if ds else 0.0
    others = sorted(diff.values())
    med = others[len(others) // 2] if others else 0
    flags = [c for c in copies if diff[c] > thr and diff[c] > 1.6 * max(med, 1)]
    line = '%-24s %s%s' % (step, '  '.join('%s %5.1f' % (c, diff[c]) for c in copies), ('   ODD: ' + ','.join(flags)) if flags else '')
    out.append(line)
    w, h = 480, 270
    grid = Image.new('RGB', (2 * w, 2 * h), (0, 0, 0))
    for i, c in enumerate(['A', 'B', 'C', 'D']):
        if c not in shots:
            continue
        im = Image.open(shots[c]).convert('RGB').resize((w, h))
        d = ImageDraw.Draw(im)
        d.rectangle([0, 0, 150, 22], fill=(0, 0, 0))
        d.text((4, 4), '%s  diff %.1f%s' % (c, diff[c], '  ODD' if c in flags else ''), fill=(255, 80, 80) if c in flags else (255, 255, 0))
        grid.paste(im, ((i % 2) * w, (i // 2) * h))
    grid.save(os.path.join(folder, step + '_grid.png'))
with open(os.path.join(folder, 'shotgrid.txt'), 'w') as f:
    f.write('\n'.join(out) + '\n')
print('\n'.join(out))
