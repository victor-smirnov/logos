#!/usr/bin/env python3
"""genfix.py — land the round's caught hand programs as ordinary fixtures (pass: exit 0 + stdout; fail: diagnostic pinned).
Each program is compiled+run on the BASE copy and on the CANDIDATE; a pass fixture is written only when the candidate exits 0
(after the WANT fix) and its stdout equals the rustc twin's stdout."""
import os, re, subprocess, sys
ROOT = "/home/logos/devel/logos"
D = "/tmp/claude-1004/-home-logos-devel-logos/25aa8421-fce1-4a11-8a89-5d2ba5981c88/scratchpad/land.AXu9"
BASE = (f"{D}/base/logosc", f"{D}/base/lib")
CAND = (f"{ROOT}/build-dev0915d/bin/logosc", f"{ROOT}/build-dev0915d/lib/logos")
PR = f"{ROOT}/src/compiler/probes/2026-09-15e-consume"
MR = f"{ROOT}/src/compiler/probes/2026-09-15f-consumeland"
RUSTC = "/home/victor/.rustup/toolchains/stable-x86_64-unknown-linux-gnu/bin/rustc"
PASS = f"{ROOT}/tests/logos/pass"; FAIL = f"{ROOT}/tests/logos/fail"

def hrun(tool, src):
    out = subprocess.run(["bash", f"{D}/hrun.sh", tool[0], tool[1], src], capture_output=True, text=True).stdout.strip()
    return out

def field(line, key):
    m = re.search(key + r"=(.*?)(?= (?:run|out|diag|vg)=|$)", line)
    return m.group(1).strip() if m else ""

def reflow_oneliners(src):
    # multi-line the one-line `fn g(...) { a; b; }` bodies of the pricing battery (statement split at "; ").
    out = []
    for l in src.splitlines():
        m = re.match(r"^(fn g\(.*?\) -> i64) \{ (.*) \}$", l)
        if m:
            body = m.group(2).split("; ")
            out.append(m.group(1) + " {")
            for i, s in enumerate(body):
                out.append("    " + s + (";" if i < len(body) - 1 and not s.endswith(";") else ""))
            out.append("}")
        else:
            out.append(l)
    return "\n".join(out) + "\n"

def rust_stdout(rs):
    b = f"{D}/rbin_fix/{os.path.basename(rs)[:-3]}"
    os.makedirs(os.path.dirname(b), exist_ok=True)
    r = subprocess.run([RUSTC, "--edition", "2024", "-o", b, rs], capture_output=True, text=True)
    if r.returncode != 0:
        return None, [l for l in r.stderr.splitlines() if l.startswith("error")][:1]
    p = subprocess.run([b], capture_output=True, text=True)
    return p.stdout.strip(), p.returncode

def land_pass(srcpath, rid, caught, rs, want_fix=None):
    src = open(srcpath).read()
    if want_fix:
        src = src.replace(want_fix[0], want_fix[1])
    src = reflow_oneliners(src)
    name = f"bc_0915f_consumeland_hb_{rid}_admit"
    src = re.sub(r"^package \w+;", f"package {name};", src, count=1, flags=re.M)
    rout, rrc = rust_stdout(rs)
    hdr = (f"// hand battery: round 2026-09-15f-consumeland, program {rid} — caught: {caught}\n" +
           (f"// legality: rustc 1.98.1 --edition 2024 on the twin {rs[len(ROOT)+1:]}: compiles, runs, stdout `{rout}`\n" if rout else f"// legality: rustc 1.98.1 --edition 2024 on the twin {rs[len(ROOT)+1:]}: compiles, runs, exit 0 (the twin asserts the same destructor count)\n"))
    tmp = f"{D}/fixgen/{name}.logos"
    os.makedirs(os.path.dirname(tmp), exist_ok=True)
    open(tmp, "w").write(hdr + src)
    b = hrun(BASE, tmp); c = hrun(CAND, tmp)
    cout = field(c, "out"); crun = field(c, "run")
    ok = crun == "0" and rout is not None and (cout == rout or (rout == "" and rrc == 0))
    print(f"{'LAND' if ok else 'SKIP'} {name}: base[{field(b,'run')} {field(b,'out')}] cand[{crun} {cout}] rust[{rout}]")
    if ok:
        open(f"{PASS}/{name}.logos", "w").write(hdr + src)
        open(f"{PASS}/{name}.expected", "w").write(f"exit: 0\nstdout: {cout}\n")

def land_fail(srcpath, rid, caught, rs, pin):
    src = open(srcpath).read()
    name = f"bc_0915f_consumeland_hb_{rid}_refuse"
    src = re.sub(r"^package \w+;", f"package {name};", src, count=1, flags=re.M)
    rout, rerr = rust_stdout(rs)
    hdr = (f"// hand battery: round 2026-09-15f-consumeland, program {rid} — caught: {caught}\n"
           f"// legality: rustc 1.98.1 --edition 2024 on the twin {rs[len(ROOT)+1:]}: REFUSED {rerr[0] if rout is None else 'NOT REFUSED?'}\n")
    tmp = f"{D}/fixgen/{name}.logos"
    os.makedirs(os.path.dirname(tmp), exist_ok=True)
    open(tmp, "w").write(hdr + src)
    err = subprocess.run([CAND[0], tmp, "-o", "/dev/null"], capture_output=True, text=True, env=dict(os.environ, LOGOS_LIB_DIR=CAND[1])).stderr
    ok = rout is None and pin in err
    print(f"{'LAND' if ok else 'SKIP'} {name}: pin={'in' if pin in err else 'NOT in'} cand stderr: {err.strip().splitlines()[:1]}")
    if ok:
        open(f"{FAIL}/{name}.logos", "w").write(hdr + src)
        open(f"{FAIL}/{name}.expected", "w").write(pin + "\n")

if __name__ == "__main__":
    MOVED_PRICING = "o01 o03 o05 o06 o08 b03 b11 b12 b16 b20 c07 c08 c09 c11 c12 c13 d11 d12 e05 e06".split()
    import glob
    for rid in MOVED_PRICING:
        sp = glob.glob(f"{PR}/battery/{rid}_*.logos")[0]
        rs = f"{PR}/rust/{os.path.basename(sp)[:-6]}.rs"
        land_pass(sp, rid, "its destructor count moved to Rust's answer between base 7eb6735b16576c45 and the landing (a by-value operator / *Assign operand now marked moved); pricing round 2026-09-15e-consume's program, statements re-flowed one per line", rs)
    if len(sys.argv) > 1 and sys.argv[1] == "pricing-only": sys.exit(0)
    MINE_MOVED = {"k03": None, "k04": None, "k12": None, "k16": ("got != 1121i64", "got != 111i64"),
                  "k17": ("got != 1221i64", "got != 1111i64"), "k20": ("got != 11i64", "got != 111i64"),
                  "u04": None, "u05": None, "v01": None, "v02": None, "v04": None, "v05": None, "v06": None, "v07": None,
                  "v08": None, "v11": None, "v14": None}
    for rid, wf in MINE_MOVED.items():
        sp = glob.glob(f"{D}/c2/{rid}_*.logos") + glob.glob(f"{D}/v/{rid}_*.logos")
        sp = sp[0]
        rs = f"{MR}/rust/{os.path.basename(sp)[:-6]}.rs"
        land_pass(sp, rid, "its destructor count moved to Rust's answer between base 7eb6735b16576c45 and the landing (a by-value operator / *Assign operand now marked moved)" + ("; base segfaulted (rc 139, valgrind 6 errors)" if rid == "v14" else ""), rs, wf)
    REFUTERS = ["k08", "k09", "k10", "t11", "t13", "u07"]
    for rid in REFUTERS:
        sp = glob.glob(f"{D}/c2/{rid}_*.logos")[0]
        rs = f"{MR}/rust/{os.path.basename(sp)[:-6]}.rs"
        land_pass(sp, rid, "REFUTED the pricing's recommended arm: right on base and on the landing, but LEAKED its whole array (n -> 0) under the first candidate b3b9ae03 that marked array-literal elements moved at SemaChecker::lower_arr_lit / lower_arr_fill_lit — right on base only by cancellation (row aggregate_literal_temp_place_base_never_dropped_run)", rs)
