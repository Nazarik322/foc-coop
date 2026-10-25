# Diffs two `fulldump` files (host vs guest, B5 rest): classes whose live object count differs, objects only on one side,
# and property values that differ (pointers / raw words summarised by class name only).
# usage: python fulldiff.py <host fd_A.txt> <guest fd_B.txt> [max lines per section]
import sys, re
from collections import Counter, defaultdict


def load(path):
    classes, objs, cur = {}, {}, None
    for line in open(path, encoding='latin1'):
        line = line.rstrip('\n')
        if line.startswith('CLASS '):
            _, n, c = line.rsplit(' ', 2)[0], line.split(' ')[1], line.split(' ')[-1]
            classes[line.split(' ')[1]] = int(c)
        elif line.startswith('OBJ '):
            key = line.split(' ')[1]; cur = objs.setdefault(key, {})
        elif line.startswith('  ') and cur is not None and ' = ' in line:
            k, v = line.strip().split(' = ', 1)
            v = re.sub(r'\s+@[0-9a-f/]+$', '', v)     # property offset / bool mask
            v = re.sub(r'_\d+\)', ')', v)            # object labels: drop instance numbers (differ between host and guest)
            cur[k] = v
    return classes, objs


def main():
    a, b = load(sys.argv[1]), load(sys.argv[2])
    lim = int(sys.argv[3]) if len(sys.argv) > 3 else 60
    print('== class counts host vs guest (different)')
    rows = [(c, a[0].get(c, 0), b[0].get(c, 0)) for c in set(a[0]) | set(b[0]) if a[0].get(c, 0) != b[0].get(c, 0)]
    rows.sort(key=lambda r: -abs(r[1] - r[2]))
    for c, x, y in rows[:lim]: print('  %-48s host %6d guest %6d' % (c, x, y))
    print('== objects only on host'); n = 0
    for k in sorted(set(a[1]) - set(b[1])):
        if n < lim: print('  ' + k)
        n += 1
    print('  (%d)' % n)
    print('== objects only on guest'); n = 0
    for k in sorted(set(b[1]) - set(a[1])):
        if n < lim: print('  ' + k)
        n += 1
    print('  (%d)' % n)
    print('== property differences, grouped by (object class, property): count, example host -> guest')
    grp = defaultdict(list)
    for k in set(a[1]) & set(b[1]):
        oa, ob = a[1][k], b[1][k]
        cls = k.split('|')[1]
        for p in set(oa) | set(ob):
            va, vb = oa.get(p, '<none>'), ob.get(p, '<none>')
            if va != vb: grp[(cls, p)].append((k, va, vb))
    for (cls, p), lst in sorted(grp.items(), key=lambda kv: -len(kv[1])):
        k, va, vb = lst[0]
        print('  %4d  %s  %s :  %s  ->  %s   [%s]' % (len(lst), cls, p, va[:60], vb[:60], k[:70]))


main()
