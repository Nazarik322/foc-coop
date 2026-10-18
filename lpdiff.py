import re, sys
path = sys.argv[1]
slices = {}
for l in open(path, encoding='utf-8', errors='replace'):
    m = re.search(r'INST words (t\+\d+s) ([0-9A-F]+): (.*)$', l)
    if not m:
        continue
    t, addr, rest = m.groups()
    d = {}
    for kv in rest.split():
        o, v = kv.split(':')
        d[int(o, 16)] = int(v, 16)
    slices.setdefault((addr, t), d)
keys = sorted(slices.keys())
print([k for k in keys])
# default object = first address; instance = second
addrs = sorted(set(a for a, t in keys))
print(addrs)
if len(addrs) >= 2:
    dflt = slices[(addrs[0], 't+20s')] if (addrs[0], 't+20s') in slices else None
    for a in addrs[1:]:
        for t in ('t+0s', 't+10s', 't+20s'):
            if (a, t) not in slices:
                continue
            inst = slices[(a, t)]
            print('== instance', a, t, 'words', len(inst))
            diffs = []
            for o in sorted(set(inst) | set(dflt)):
                iv = inst.get(o, 0)
                dv = dflt.get(o, 0)
                if iv != dv and o >= 0x2c0:
                    diffs.append((o, dv, iv))
            print(' '.join('%x:%x->%x' % d for d in diffs[:120]))
