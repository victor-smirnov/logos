#!/usr/bin/env python3
"""lt_discover — derive lt's test table from the source tree.

`discover(src_root, build_dir)` returns one dict per test with the columns of
lt's `tests` table:

  name, command, env, workdir, timeout, labels, source, disabled, processors,
  skip_rc, fixtures_setup, fixtures_required, depends

WHAT the tests are lives in tests/lt_registry.py (singletons + family rules).
This file is the engine: it expands the registry's placeholders, walks the
globs, fills the defaults ctest would have filled, and derives the two columns
ctest itself derived (`source` the way `lt sync` computes it, `depends` from the
fixtures).

  python3 scripts/lt_discover.py --snapshot F       write the normalized table to F
  python3 scripts/lt_discover.py --diff F [-n N]    diff the discovered table against snapshot F
  python3 scripts/lt_discover.py --json           dump the discovered table
"""
import argparse, fnmatch, json, os, re, shutil, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_TIMEOUT = 1500.0          # scripts/lt: a test with no TIMEOUT property


def load_registry(src_root):
    import importlib.util
    path = os.path.join(src_root, "tests", "lt_registry.py")
    spec = importlib.util.spec_from_file_location("lt_registry", path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def read_cmake_cache(build_dir):
    cache = {}
    try:
        with open(os.path.join(build_dir, "CMakeCache.txt"), errors="replace") as f:
            for line in f:
                if line.startswith(("#", "//")) or "=" not in line or ":" not in line.split("=", 1)[0]:
                    continue
                key, val = line.rstrip("\n").split("=", 1)
                cache[key.split(":", 1)[0]] = val
    except OSError:
        pass
    return cache


def cmake_truthy(val):
    return (val or "").upper() in ("1", "ON", "YES", "TRUE", "Y") or \
        (val or "").replace(".", "").isdigit() and float(val) != 0


class Ctx:
    """What a registry family may ask of the tree."""

    def __init__(self, src, build):
        self.src, self.build = src, build
        self.cache = read_cmake_cache(build)
        self.warnings = []
        self.v = {
            "src": src,
            "build": build,
            "tsrc": src + "/tests/logos",
            "tbin": build + "/tests/logos",
            "logosc": build + "/bin/logosc",
            # LOGOS_BUILD_LIB_DIR = ${CMAKE_BINARY_DIR}/${LOGOS_LIB_RELDIR}
            "libdir": build + "/" + self.cache.get("LOGOS_LIB_RELDIR", "lib/logos"),
            # ctest resolves a bare command name (`bash`, `python3`) through PATH.
            "bash": shutil.which("bash") or "bash",
            "python3": shutil.which("python3") or "python3",
            "cmake": self.cache.get("CMAKE_COMMAND") or shutil.which("cmake") or "cmake",
            "ctest": self.cache.get("CMAKE_CTEST_COMMAND") or shutil.which("ctest") or "ctest",
            "filecheck": self.find_filecheck(),
        }

    def find_filecheck(self):
        # The search tests/logos/CMakeLists.txt used to do: find_program(
        # LOGOS_FILECHECK_BIN NAMES FileCheck-<major> FileCheck HINTS
        # <llvm dir>/../../../bin /usr/lib/llvm-<major>/bin). A cache that still
        # carries the old configured answer wins; a fresh one has no such entry.
        if "LOGOS_FILECHECK_BIN" in self.cache:
            hit = self.cache["LOGOS_FILECHECK_BIN"]
            return "" if hit.endswith("-NOTFOUND") else hit
        major = self.cache.get("LOGOS_LLVM_MAJOR", "")
        if not major:
            # root CMakeLists.txt: a non-cached LOGOS_LLVM_MAJOR is derived from
            # an explicit LOGOS_LLVM_DIR matching "llvm-([0-9]+)".
            m = re.search(r"llvm-([0-9]+)", self.cache.get("LOGOS_LLVM_DIR", ""))
            major = m.group(1) if m else ""
        names = (["FileCheck-" + major] if major else []) + ["FileCheck"]
        hints = []
        if self.cache.get("LOGOS_LLVM_DIR"):
            hints.append(os.path.normpath(self.cache["LOGOS_LLVM_DIR"] + "/../../../bin"))
        if major:
            hints.append("/usr/lib/llvm-%s/bin" % major)
        for n in names:
            for h in hints:
                if os.access(os.path.join(h, n), os.X_OK):
                    return os.path.join(h, n)
            if shutil.which(n):
                return shutil.which(n)
        return ""

    def option(self, name):
        return cmake_truthy(self.cache.get(name))

    # CMake `file(GLOB <dir>/<pat>)`: a flat directory, dotfiles included,
    # directories excluded, sorted (every glob in the CMakeLists is list(SORT)ed
    # or order-irrelevant).
    def glob(self, rel_pattern):
        d, pat = os.path.split(os.path.join(self.src, rel_pattern))
        if "*" in d:                      # one wildcard directory level
            parent, dpat = os.path.split(d)
            dirs = [os.path.join(parent, x) for x in self._ls(parent)
                    if fnmatch.fnmatchcase(x, dpat) and os.path.isdir(os.path.join(parent, x))]
        else:
            dirs = [d]
        out = []
        for dd in dirs:
            out += [os.path.join(dd, x) for x in self._ls(dd)
                    if fnmatch.fnmatchcase(x, pat) and not os.path.isdir(os.path.join(dd, x))]
        return sorted(out)

    # CMake `file(GLOB_RECURSE <dir>/*<suffix>)` (symlinked dirs not followed).
    def glob_recurse(self, rel_dir, suffix):
        out = []
        for dp, dns, fs in os.walk(os.path.join(self.src, rel_dir)):
            out += [os.path.join(dp, f) for f in fs if f.endswith(suffix)]
        return sorted(out)

    @staticmethod
    def _ls(d):
        try:
            return os.listdir(d)
        except OSError:
            return []

    def read_bytes(self, path):
        with open(path, "rb") as f:
            return f.read()

    def warn(self, msg):
        self.warnings.append(msg)


def test_source(cmd, src):
    """scripts/lt `test_source`: the first `.logos` argument, else the first
    `.sh`/`.py` under the source root after argv[0]."""
    for a in cmd:
        if a.endswith(".logos"):
            return os.path.relpath(a, src) if a.startswith(src) else a
    for a in cmd[1:] if len(cmd) > 1 else cmd:
        if a.startswith(src) and (a.endswith(".sh") or a.endswith(".py")):
            return os.path.relpath(a, src)
    return ""


def expand(s, v):
    return s.format(**v)


def finish(t, workdir, src):
    """Fill the columns ctest/lt default and derive `source`."""
    return {
        "name": t["name"],
        "command": list(t["command"]),
        "env": list(t.get("env", [])),
        "workdir": t.get("workdir") or workdir,
        "timeout": float(t["timeout"]) if t.get("timeout") is not None else DEFAULT_TIMEOUT,
        "labels": list(t.get("labels", [])),
        "source": test_source(t["command"], src),
        "disabled": 1 if t.get("disabled") else 0,
        "processors": int(t.get("processors", 1)),
        "skip_rc": t.get("skip_rc"),
        "fixtures_setup": list(t.get("fixtures_setup", [])),
        "fixtures_required": list(t.get("fixtures_required", [])),
        "depends": list(t.get("depends", [])),
    }


def check_tiers(tests, reg):
    """The tier rule (formerly the configure-time audit in the deleted
    cmake/LogosTestTiers.cmake): every non-corpus test carries exactly one tier
    label."""
    bad = []
    for t in tests:
        if "corpus" in t["labels"]:
            continue
        n = sum(1 for x in reg.TIER_VALUES if x in t["labels"])
        if n != 1:
            bad.append("%s — carries %d tier labels, needs exactly 1 (has: %s)"
                       % (t["name"], n, ", ".join(t["labels"])))
    return bad


def discover(src_root, build_dir):
    src = os.path.abspath(src_root)
    build = os.path.abspath(build_dir)
    reg = load_registry(src)
    ctx = Ctx(src, build)
    tests = []
    for rel, entries in reg.SINGLETONS.items():
        wd = build if rel == "." else build + "/" + rel
        for e in entries:
            t = dict(e)
            t["command"] = [expand(a, ctx.v) for a in e["command"]]
            t["env"] = [expand(a, ctx.v) for a in e["env"]]
            tests.append(finish(t, wd, src))
    for rel, fam in reg.FAMILIES:
        wd = build if rel == "." else build + "/" + rel
        tests += [finish(t, wd, src) for t in fam(ctx)]

    names = {}
    for t in tests:
        if t["name"] in names:
            raise SystemExit("lt_discover: duplicate test name %s" % t["name"])
        names[t["name"]] = t
    bad = check_tiers(tests, reg)
    if bad:
        raise SystemExit("lt_discover: TIER RULE (tests/lt_registry.py TIER_VALUES)\n  "
                         + "\n  ".join(bad))
    # ctest: a test requiring a fixture DEPENDS on every test that sets it up.
    setups = {}
    for t in tests:
        for f in t["fixtures_setup"]:
            setups.setdefault(f, []).append(t["name"])
    for t in tests:
        for f in t["fixtures_required"]:
            t["depends"] += [n for n in setups.get(f, []) if n not in t["depends"]]
    for w in ctx.warnings:
        print("lt_discover: warning: " + w, file=sys.stderr)
    return tests


# ── --snapshot / --diff ──────────────────────────────────────────────────────────────────

SET_FIELDS = ("labels", "fixtures_setup", "fixtures_required", "depends")


def normalize(t, src, build):
    def n(s):
        if not isinstance(s, str):
            return s
        # the build dir lies under the source root, so it is replaced first
        return s.replace(build, "${BUILD}").replace(src, "${SRC}")
    out = {}
    for k, val in t.items():
        out[k] = [n(x) for x in val] if isinstance(val, list) else n(val)
    return out


def snapshot(path):
    """Write the discovered table, normalized (${SRC} / ${BUILD}), for a later
    --diff: the check that a registry refactor changed no test."""
    src, build = ROOT, os.path.join(ROOT, "build")
    with open(path, "w") as f:
        json.dump({t["name"]: normalize(t, src, build) for t in discover(src, build)},
                  f, indent=0, sort_keys=True)


def check(n_examples, path):
    src, build = ROOT, os.path.join(ROOT, "build")
    with open(path) as f:
        truth = json.load(f)
    got = {t["name"]: normalize(t, src, build) for t in discover(src, build)}
    only_got = sorted(set(got) - set(truth))
    only_truth = sorted(set(truth) - set(got))
    print("discovered %d tests, snapshot %d" % (len(got), len(truth)))
    print("only in discovery: %d" % len(only_got))
    for x in only_got[:n_examples]:
        print("    " + x)
    print("only in snapshot:  %d" % len(only_truth))
    for x in only_truth[:n_examples]:
        print("    " + x)
    diffs = {}
    for name in sorted(set(got) & set(truth)):
        a, b = got[name], truth[name]
        for k in b:
            if k == "name":
                continue
            va, vb = a.get(k), b[k]
            same = (sorted(va or []) == sorted(vb or [])) if k in SET_FIELDS else va == vb
            if not same:
                diffs.setdefault(k, []).append((name, va, vb))
    fields = sorted(set(truth[next(iter(truth))]) - {"name"}) if truth else []
    total = 0
    print("field differences on %d common tests:" % len(set(got) & set(truth)))
    for k in fields:
        lst = diffs.get(k, [])
        total += len(lst)
        print("  %-18s %d" % (k, len(lst)))
        for name, va, vb in lst[:n_examples]:
            print("      %s\n        discovered: %r\n        snapshot:   %r" % (name, va, vb))
    bad = len(only_got) + len(only_truth) + total
    print("TOTAL DIFFERENCES: %d" % bad)
    return 1 if bad else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--snapshot", metavar="FILE", help="write the normalized table to FILE")
    ap.add_argument("--diff", metavar="FILE", help="diff the discovered table against a --snapshot FILE")
    ap.add_argument("--json", action="store_true", help="print the discovered table")
    ap.add_argument("-n", type=int, default=5, help="examples per difference kind")
    ap.add_argument("--src", default=ROOT)
    ap.add_argument("--build", default=None)
    # ctest -L / -LE semantics: a regular expression searched in each label.
    ap.add_argument("--label", action="append", default=[], help="keep tests with a label matching RE")
    ap.add_argument("--label-exclude", action="append", default=[], help="drop tests with a label matching RE")
    ap.add_argument("--count", action="store_true", help="print only the number of (filtered) tests")
    args = ap.parse_args()
    if args.snapshot:
        snapshot(args.snapshot)
        return
    if args.diff:
        sys.exit(check(args.n, args.diff))
    tests = discover(args.src, args.build or os.path.join(args.src, "build"))
    import re as _re
    for rx in args.label:
        tests = [t for t in tests if any(_re.search(rx, l) for l in t["labels"])]
    for rx in args.label_exclude:
        tests = [t for t in tests if not any(_re.search(rx, l) for l in t["labels"])]
    if args.count:
        print(len(tests))
        return
    if args.json:
        json.dump(tests, sys.stdout, indent=1)
        print()
    else:
        print("%d tests" % len(tests))


if __name__ == "__main__":
    main()
