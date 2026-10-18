import sys, numpy as np

D = r'E:\FoC-coop\A\Binaries'


def load(name):
    idx = {}
    for l in open(f'{D}\\{name}.idx'):
        b, sz, off = l.split()
        idx[int(b, 16)] = (int(sz, 16), int(off, 16))
    data = np.memmap(f'{D}\\{name}.bin', dtype=np.uint8, mode='r')
    return idx, data


i1, d1 = load('foc_snap1')
i2, d2 = load('foc_snap2')
print('regions', len(i1), len(i2))
cands = []
nreg = 0
for base, (sz, off1) in i1.items():
    if base not in i2:
        continue
    sz2, off2 = i2[base]
    n = min(sz, sz2) // 4 * 4
    if n == 0:
        continue
    nreg += 1
    a = np.frombuffer(d1[off1:off1 + n], dtype=np.uint32)
    b = np.frombuffer(d2[off2:off2 + n], dtype=np.uint32)
    diff = np.nonzero(a != b)[0]
    if len(diff) == 0:
        continue
    av = a[diff].astype(np.uint64)
    bv = b[diff].astype(np.uint64)
    x = av ^ bv
    # single-bit change that SETS a bit, new value small
    single = (x & (x - np.uint64(1))) == 0
    sets = (bv & x) != 0
    small = bv < np.uint64(0x100)
    sel = np.nonzero(single & sets & small)[0]
    for k in sel:
        pos = int(diff[k])
        cands.append((base + pos * 4, int(av[k]), int(bv[k])))
print('compared regions', nreg, 'candidates', len(cands))
# summarize by new value
from collections import Counter
print(Counter((c[1], c[2]) for c in cands).most_common(15))
# save
with open(r'C:\GitRepo\coop-anything\local\snap_cands.txt', 'w') as f:
    for a, o, n in cands:
        f.write('%08x %x %x\n' % (a, o, n))
