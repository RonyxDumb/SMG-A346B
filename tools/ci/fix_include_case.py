#!/usr/bin/env python3
"""Rewrites #include paths whose letter case does not match the file on disk.

The sources were developed on a case-insensitive file system; Linux (CI)
resolves includes case-sensitively.  Run from the repository root:
    python3 tools/ci/fix_include_case.py [--check]
"""
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
INCLUDE_DIRS = [
    'platform/include', 'platform/src', 'platform/mobile', 'decomp/include', 'decomp/libs/JSystem/include',
    'decomp/libs/RVL_SDK/include', 'decomp/libs/nw4r/include', 'decomp/libs/RVLFaceLib/include',
]
SOURCE_DIRS = ['decomp', 'platform']
EXTS = ('.c', '.cpp', '.h', '.hpp', '.inc', '.inl')
INCLUDE_RE = re.compile(r'^(\s*#\s*include\s*)(["<])([^">]+)([">])', re.M)

_listing = {}


def listing(d):
    if d not in _listing:
        try:
            _listing[d] = {n.lower(): n for n in os.listdir(d)}
        except OSError:
            _listing[d] = {}
    return _listing[d]


def resolve(base, rel):
    """The path `rel` under `base` with the case found on disk, or None."""
    cur = base
    out = []
    for part in rel.replace('\\', '/').split('/'):
        if part in ('', '.'):
            continue
        if part == '..':
            cur = os.path.dirname(cur)
            out.append('..')
            continue
        real = listing(cur).get(part.lower())
        if real is None:
            return None
        out.append(real)
        cur = os.path.join(cur, real)
    return '/'.join(out) if os.path.isfile(cur) else None


def main():
    check = '--check' in sys.argv
    changed = 0
    for sd in SOURCE_DIRS:
        for dirpath, _, files in os.walk(os.path.join(ROOT, sd)):
            for f in files:
                if not f.endswith(EXTS):
                    continue
                path = os.path.join(dirpath, f)
                with open(path, 'rb') as fh:
                    raw = fh.read()
                text = raw.decode('latin-1')

                def fix(m):
                    rel = m.group(3)
                    norm = rel.replace('\\', '/')
                    for base in [dirpath] + [os.path.join(ROOT, d) for d in INCLUDE_DIRS]:
                        real = resolve(base, norm)
                        if real is not None:
                            return m.group(0) if real == rel else m.group(1) + m.group(2) + real + m.group(4)
                    return m.group(0)  # a system header

                new = INCLUDE_RE.sub(fix, text)
                if new != text:
                    changed += 1
                    print(('would fix ' if check else 'fixed ') + os.path.relpath(path, ROOT))
                    if not check:
                        with open(path, 'wb') as fh:
                            fh.write(new.encode('latin-1'))
    print('%d file(s) with include case mismatches' % changed)
    return 1 if (check and changed) else 0


if __name__ == '__main__':
    sys.exit(main())
