#!/usr/bin/env python3
"""run_oracle.py <outfile> — THE RUNTIME COST COLUMN.

⚠ WHY. `ceiling-probe.sh` prices COST over `-L bc -L pass` plus three
directories: 1047 of the tree's 6468 registered `pass` tests. A drop-glue or a
codegen change is not visible in a compile's exit code at all — the five
defects this file was written for ALL compile rc 0 and do the wrong thing at
RUN TIME. So the unit here is a TRIPLE taken from a program that is compiled,
LINKED and EXECUTED:

    ccrc    logosc's exit code (90 = exited 0 after self-diagnosing, the 14th
            recorded gate lie)
    runrc   the compiled program's exit code
    sha     sha256 of its stdout

The population is lt's discovery filtered `-L pass`, read from the registered command lines
for the same reason fail_text_oracle.py reads them: the label is decided in
CMake and a glob here would be a drifting second copy.
"""
import subprocess, shlex, re, sys, os, hashlib, tempfile, shutil, concurrent.futures, glob

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.environ.get("LOGOS_BUILD", os.path.join(ROOT, "build"))
SEL = os.environ.get("LOGOS_RUN_ORACLE_SEL", "-L pass").split()
LIB = os.path.join(BUILD, "lib", "logos")

def archives():
    a = sorted(glob.glob(os.path.join(LIB, "liblstdlib*.a")))
    a += sorted(glob.glob(os.path.join(LIB, "liblogos-*.a")))
    a += [p for p in sorted(glob.glob(os.path.join(LIB, "*.a"))) if p not in a]
    return a
ARCH = archives()

def population():
    # The registry is lt's discovery (scripts/lt_discover.py); ctest is retired.
    # LOGOS_RUN_ORACLE_SEL keeps its meaning: `-L RE` / `-LE RE` label filters.
    sys.path.insert(0, os.path.join(ROOT, "scripts"))
    import lt_discover
    tests = lt_discover.discover(ROOT, BUILD)
    sel = list(SEL)
    while sel:
        flag, rx = sel[0], sel[1] if len(sel) > 1 else ""
        sel = sel[2:]
        if flag == "-L":
            tests = [t for t in tests if any(re.search(rx, l) for l in t["labels"])]
        elif flag == "-LE":
            tests = [t for t in tests if not any(re.search(rx, l) for l in t["labels"])]
    rows = []
    for t in tests:
        cmd = t["command"]
        env = dict(kv.split("=", 1) for kv in t.get("env", []) if kv.startswith("LOGOS_") and "=" in kv)
        # run_test.sh MODE LOGOSC TEST_LOGOS EXPECTED EXTRA... (a pass fixture
        # with no EXTRA flags has a FIVE-element command line).
        if len(cmd) >= 5 and cmd[1] == "pass":
            rows.append((t["name"], cmd[3], cmd[5:], env))
    return rows

def one(row):
    name, src, extra, env = row
    e = dict(os.environ); e.update(env); e.setdefault("LOGOS_LIB_DIR", LIB)
    d = tempfile.mkdtemp()
    try:
        p = subprocess.run([os.path.join(BUILD, "bin", "logosc"), src, "-o", d + "/f.o"] + extra,
                           capture_output=True, text=True, env=e, cwd=BUILD, timeout=300)
        cc = p.returncode
        # ⚠ `warning:` IS NOT A SELF-DIAGNOSED MALFUNCTION, AND CONFLATING THE TWO
        # COST THIS COLUMN 20 PROGRAMS. `cc = 90` exists because of the 14th gate
        # lie — the compiler printed `mlir_gen:`, WROTE THE OBJECT and exited 0 —
        # and everything below `if cc != 0` never links and never RUNS. But the
        # compiler prints eight distinct `<stage>: warning:` forms, more than any
        # other shape, so a program that merely warns was filed as malfunctioning
        # and silently left out of the run population. Measured 2026-09-11:
        # 6660 rows, 20 of them c90, 16 matching only on a `warning:` line while
        # compiling rc 0 — and the column's silence about them had been read as
        # coverage for the whole arc. Narrowed to exclude `warning:` only; every
        # other spelling (`internal:`, `unsupported`, `module verification
        # failed`, `note`, …) still counts as a malfunction.
        if cc == 0 and re.search(r'^(mlir_gen|sema|mono): (?!warning:)',
                                 p.stderr + p.stdout, re.M):
            cc = 90          # exited 0 after self-diagnosing
        if cc != 0:
            return (name, cc, "-", "-")
        lk = subprocess.run(["cc", d + "/f.o", "-Wl,--start-group"] + ARCH +
                            ["-Wl,--end-group", "-lpthread", "-lm", "-lstdc++",
                             "-Wl,--gc-sections", "-Wl,--allow-multiple-definition",
                             "-o", d + "/f.bin"], capture_output=True, timeout=300)
        if lk.returncode != 0:
            return (name, cc, "LINK", "-")
        r = subprocess.run([d + "/f.bin"], capture_output=True, timeout=120, cwd=d)
        return (name, cc, str(r.returncode),
                hashlib.sha256(r.stdout).hexdigest()[:16])
    except subprocess.TimeoutExpired:
        return (name, 99, "TIMEOUT", "-")
    except Exception as ex:
        return (name, 98, "ERR:" + type(ex).__name__, "-")
    finally:
        shutil.rmtree(d, ignore_errors=True)

def main():
    # ⚠ THREE FALSE MEASUREMENTS CAME OUT OF THIS ONE ARGUMENT, so it is checked.
    #   * driven with a `>` redirect instead of argv[1]: the table was written to a
    #     file named by whatever argv[1] happened to be, and the join against it
    #     read "19+ fixtures damaged" — a per-site read of the first said rc 0 both
    #     armed and unarmed (2026-09-11);
    #   * invoked bare: a file literally named `--help` appeared in the tree, twice;
    #   * with a RELATIVE `LOGOS_BUILD`: 6627 rows of `ERR:FileNotFoundError` and a
    #     diff of ZERO movers — a null result through a broken channel, which reads
    #     exactly like a clean measurement (2026-09-10).
    # A cost column that can be wrong in the reassuring direction must refuse rather
    # than produce a table.
    if len(sys.argv) != 2 or sys.argv[1].startswith("-"):
        print(__doc__.strip().splitlines()[0], file=sys.stderr)
        print("run-oracle: needs exactly one argument, the OUTPUT PATH. A `>` "
              "redirect does not work: the table is written to argv[1], and the "
              "join then reads phantom damage.", file=sys.stderr)
        return 2
    out = sys.argv[1]
    lb = os.environ.get("LOGOS_BUILD")
    if lb and not os.path.isabs(lb):
        print("run-oracle: LOGOS_BUILD=%r is RELATIVE. Every compile would fail with "
              "FileNotFoundError and the diff would show zero movers — a null result "
              "through a broken channel. Pass an absolute path." % lb, file=sys.stderr)
        return 2
    rows = population()
    if not rows:
        print("run-oracle: the selection %r names NO pass fixture" % " ".join(SEL), file=sys.stderr)
        return 2
    with concurrent.futures.ThreadPoolExecutor(max_workers=int(os.environ.get("LOGOS_RUN_ORACLE_JOBS", os.cpu_count()))) as ex:
        res = sorted(ex.map(one, rows))
    with open(out, "w") as f:
        for name, cc, rr, sha in res:
            f.write("%s\t%s\t%s\t%s\n" % (name, cc, rr, sha))
    print("run-oracle: %d pass fixtures compiled, linked and RUN -> %s" % (len(res), out),
          file=sys.stderr)
    return 0

if __name__ == "__main__":
    sys.exit(main())
