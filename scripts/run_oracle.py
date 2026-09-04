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

The population is `ctest -N -V -L pass`, read from the registered command lines
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
    out = subprocess.run(["ctest", "--test-dir", BUILD, "-N", "-V"] + SEL,
                         capture_output=True, text=True).stdout
    rows, cmd, env = [], None, {}
    for l in out.splitlines():
        if "Test command:" in l:
            cmd = shlex.split(l.split("Test command:", 1)[1].strip()); env = {}
        m = re.match(r'\s*\d+:\s+(LOGOS_\w+)=(.*)$', l)
        if m: env[m.group(1)] = m.group(2)
        m = re.match(r'\s*Test\s+#\d+:\s+(\S+)\s*$', l)
        if m and cmd:
            # run_test.sh MODE LOGOSC TEST_LOGOS EXPECTED EXTRA...
            # ⚠ `>= 6` HERE READ 220 OF 6269. A pass fixture with no EXTRA flags
            # has a FIVE-element command line, and the bound written for the
            # fail oracle's shape silently dropped 96% of the population — a
            # filter is not a population, and a smaller one still returns a
            # confident number.
            if len(cmd) >= 5 and cmd[1] == "pass":
                rows.append((m.group(1), cmd[3], cmd[5:], dict(env)))
            cmd = None
    return rows

def one(row):
    name, src, extra, env = row
    e = dict(os.environ); e.update(env); e.setdefault("LOGOS_LIB_DIR", LIB)
    d = tempfile.mkdtemp()
    try:
        p = subprocess.run([os.path.join(BUILD, "bin", "logosc"), src, "-o", d + "/f.o"] + extra,
                           capture_output=True, text=True, env=e, cwd=BUILD, timeout=300)
        cc = p.returncode
        if cc == 0 and re.search(r'^(mlir_gen|sema|mono): ', p.stderr + p.stdout, re.M):
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
    out = sys.argv[1]
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
