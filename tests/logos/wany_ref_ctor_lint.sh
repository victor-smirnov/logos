#!/usr/bin/env bash
# #327: no SAFE path constructs a `WAny::Ref` from an unvalidated pointer. The
# variant takes any pointer and the Writ layer later reads a tag at `ptr[-1]`, so
# a construction outside `stdlib/lang/writ/anyval.logos` must go through
# `unsafe { WAny::ref_to(p) }` (the caller states the contract) or `WAny::null()`.
# Patterns (`WAny::Ref(p) =>`, `if let WAny::Ref(p) = …`), comments and string
# literals are not constructions. A canary line proves the matcher bites.
set -euo pipefail
ROOT="${1:?repo root}"
cd "$ROOT"
python3 - "$ROOT" <<'PY'
import re, subprocess, sys
root = sys.argv[1]
def constructions(text):
    out = []
    for n, ln in enumerate(text.split('\n'), 1):
        i = 0
        while (j := ln.find('WAny::Ref(', i)) >= 0:
            pre = ln[:j]
            k = j + len('WAny::Ref('); d = 1
            while k < len(ln) and d:
                d += {'(': 1, ')': -1}.get(ln[k], 0); k += 1
            after = ln[k:].lstrip()
            comment = '//' in pre and pre[:pre.find('//')].count('"') % 2 == 0
            string = pre.count('"') % 2 == 1
            pattern = (after.startswith('=>') or after.startswith('|') or
                       re.search(r'\blet\s*$', pre) is not None or
                       (after.startswith('=') and not after.startswith('==')))
            if not (comment or string or pattern or d):
                out.append((n, ln.strip()))
            i = k
    return out
canary = 'fn f(p: *const u8) -> WAny { return WAny::Ref(p); }\nmatch w { WAny::Ref(q) => 1 }\n// WAny::Ref(x)\n'
if len(constructions(canary)) != 1:
    print('FAIL(2): canary — the matcher does not see exactly one construction in its own sample'); sys.exit(2)
files = subprocess.run(['git', 'grep', '-l', 'WAny::Ref(', '--', '*.logos'], capture_output=True,
                       text=True, cwd=root).stdout.split()
bad = []
for f in files:
    if f == 'stdlib/lang/writ/anyval.logos': continue
    for n, ln in constructions(open(f'{root}/{f}').read()):
        bad.append(f'{f}:{n}: {ln[:120]}')
if bad:
    print('FAIL: a safe construction of WAny::Ref (#327) — write `unsafe { WAny::ref_to(p) }` or `WAny::null()`:')
    print('\n'.join('  ' + b for b in bad)); sys.exit(1)
print(f'OK: no safe WAny::Ref construction outside anyval.logos ({len(files)} files scanned); canary live.')
PY
