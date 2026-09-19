#!/usr/bin/env python3
"""hash-build.py [build-dir] — one hash for everything a test run depends on.

⚠ RENAMED FROM `build_hash.py` (2026-09-19) AND THE NAME IS LOAD-BEARING: `.gitignore`
carries `build*`, which matched this file, so it was UNTRACKED — a fresh clone did not
have it, and `gate-run.sh` refuses to key a run without it, i.e. the gate-verdict store
could not be created at all. `PROBES.md` had recorded that as a known defect. Do not
rename it back under any `build…` spelling; the ignore rule will silently swallow it
again and nothing will fail until someone clones.

⚠ THE VERSION STRING IS NOT AN IDENTITY, measured 2026-08-29. `logosc --version`
reports `…-dirty.20260828T222922Z`, and that timestamp comes from CMake's
CONFIGURE step, not from the build: after `touch src/compiler/borrow_check.cpp
&& cmake --build`, the binary changed and the string did not. Its commit id is
stale for the same reason. It is a fine annotation for a human and useless as a
key.

⚠ AND THE COMPILER ALONE IS NOT ENOUGH EITHER. The first version of this key
hashed the libraries and forgot logosc itself, so a compiler-only rebuild gave a
FALSE CACHE HIT — the store said "already measured" for a binary that had never
run a test. Both directions have now bitten, an hour apart.

WHAT IS HASHED, and why each part is here:
  bin/logosc               the compiler under test
  lib/logos/**             the stdlib: passed as a `-L` search path, so its
                           CONTENTS decide what every program links against
  tests/logos/*.a          the fixture archives some tests link
The set is not a guess: `ctest -N -V` prints each registered test's real command
line, and these are the only paths under the build directory those commands name.

Prints `<16-hex> <n files>` — the hash, and how many files went into it, because
a hash over an unexpectedly small set is the failure this file is about.
"""
import hashlib, os, sys

def build_hash(build):
    parts = []
    logosc = os.path.join(build, "bin", "logosc")
    if os.path.exists(logosc):
        parts.append(logosc)
    libdir = os.path.join(build, "lib", "logos")
    for root, _, files in os.walk(libdir):
        parts += [os.path.join(root, f) for f in files]
    tdir = os.path.join(build, "tests", "logos")
    if os.path.isdir(tdir):
        parts += [os.path.join(tdir, f) for f in os.listdir(tdir) if f.endswith(".a")]
    parts.sort()
    h = hashlib.sha256()
    for p in parts:
        # The NAME goes in as well as the bytes: a file appearing or vanishing
        # must change the hash even if the remaining contents are unchanged.
        h.update(os.path.relpath(p, build).encode())
        with open(p, "rb") as f:
            for chunk in iter(lambda: f.read(1 << 20), b""):
                h.update(chunk)
    return h.hexdigest()[:16], len(parts)

if __name__ == "__main__":
    b = sys.argv[1] if len(sys.argv) > 1 else "build"
    d, n = build_hash(b)
    if n == 0:
        print(f"build_hash: nothing found under {b} — that is not a build", file=sys.stderr)
        sys.exit(2)
    print(f"{d} {n}")
