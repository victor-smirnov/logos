#!/usr/bin/env python3
"""landfix.py LOGOSC LIBDIR MODE — turn each CLOSED soundness-queue row into an ordinary fixture.

MODE=measure  : compile + link + run every named row program on LOGOSC and print id, cc rc, run rc, stdout,
                first diagnostic line. Nothing is written.
MODE=emit     : write the fixtures (pass -> tests/logos/pass, refused -> tests/logos/fail) with a header,
                the package renamed to the fixture name, and `.expected` taken from the MEASURED run.

The row -> fixture map is the table below; `kind` is what the row's OBSERVED column says today, so
`admits` rows are expected to land as FAIL fixtures and `run`/`refuses` rows as PASS fixtures.
"""
import os, subprocess, sys, tempfile, glob, shutil

ROOT = "/home/logos/devel/logos"
ROWS = [
    # row id, fixture base name, expected landed disposition
    ("aggregate_literal_temp_place_base_never_dropped_run",        "bc_0915g_aggtemp_arrlit_place_base_dropped_admit",      "pass"),
    ("return_array_lit_of_moved_locals_double_drop",               "bc_0915g_aggtemp_return_arrlit_moved_locals_admit",     "pass"),
    ("generic_array_lit_typevar_elems_double_drop",                "bc_0915g_aggtemp_generic_arrlit_typevar_elems_admit",   "pass"),
    ("array_lit_index_elem_move_out_admits",                       "bc_0915g_aggtemp_arrlit_elem_move_out_refuse",          "fail"),
    ("array_repeat_len1_noncopy_operand_double_drop_run",          "bc_0915g_aggtemp_repeat_len1_operand_moved_admit",      "pass"),
    ("generic_unbounded_typevar_reuse_after_array_literal_admits", "bc_0915g_aggtemp_typevar_reuse_after_arrlit_refuse",    "fail"),
    ("aggregate_extended_borrow_temp_never_dropped",               "bc_0915g_aggtemp_extended_borrow_temp_dropped_admit",   "pass"),
    ("extended_field_base_temp_dropped_at_let_end_run",            "bc_0915g_aggtemp_extended_field_base_block_owner_admit","pass"),
    ("array_len_builtin_discards_receiver_run",                    "bc_0915g_aggtemp_array_len_receiver_runs_admit",        "pass"),
    ("call_result_index_projection_invalid_mlir_refused",          "bc_0915g_aggtemp_call_result_projection_admit",         "pass"),
    ("nested_array_literal_index_initializer_lost_refused",        "bc_0915g_aggtemp_nested_arrlit_index_admit",            "pass"),
]

def archives(lib):
    a = sorted(glob.glob(os.path.join(lib, "liblstdlib*.a")))
    a += sorted(glob.glob(os.path.join(lib, "liblogos-*.a")))
    a += [p for p in sorted(glob.glob(os.path.join(lib, "*.a"))) if p not in a]
    return a

def run_one(logosc, lib, src, tmp):
    env = dict(os.environ, LOGOS_LIB_DIR=lib, LOGOS_VERIFY_LAYOUT="1")
    obj = os.path.join(tmp, "t.o")
    cc = subprocess.run([logosc, src, "-o", obj], capture_output=True, text=True, env=env)
    diag = ""
    for l in cc.stderr.splitlines():
        if "error" in l or "]: " in l:
            diag = l.strip()[:200]; break
    if cc.returncode != 0 or diag:
        return {"cc": cc.returncode, "run": None, "out": "", "diag": diag}
    exe = os.path.join(tmp, "t")
    ld = subprocess.run(["cc", obj, "-Wl,--start-group", *archives(lib), "-Wl,--end-group",
                         "-lpthread", "-lm", "-lstdc++", "-Wl,--gc-sections",
                         "-Wl,--allow-multiple-definition", "-o", exe], capture_output=True, text=True)
    if ld.returncode != 0:
        return {"cc": 0, "run": "LINKFAIL", "out": "", "diag": ld.stderr[-200:]}
    r = subprocess.run([exe], capture_output=True, text=True, timeout=60)
    return {"cc": 0, "run": r.returncode, "out": r.stdout, "diag": ""}

def main():
    logosc, lib, mode = sys.argv[1], sys.argv[2], sys.argv[3]
    for rid, fix, want in ROWS:
        src = os.path.join(ROOT, "tests/soundness/open", rid + ".logos")
        with tempfile.TemporaryDirectory() as tmp:
            res = run_one(logosc, lib, src, tmp)
        got = "fail" if (res["cc"] != 0 or res["diag"]) else "pass"
        flag = "OK" if got == want else "MISMATCH"
        print(f"{rid}\t{flag}\twant={want}\tgot={got}\tcc={res['cc']}\trun={res['run']}\t"
              f"out={res['out'].replace(chr(10), ' | ').strip()}\tdiag={res['diag']}")
        if mode != "emit":
            continue
        if flag == "MISMATCH":
            print(f"  !! not emitting {fix}: measured disposition disagrees with the table")
            continue
        body = open(src).read().splitlines()
        # drop the row's leading comment block; keep the program from `package` on
        i = next(k for k, l in enumerate(body) if l.startswith("package "))
        prog = body[i:]
        prog[0] = f"package {fix};"
        prog = [l for l in prog if not l.startswith("// rustc 1.98.1") or True]
        hdr = [f"// soundness queue row {rid} — CLOSED by round 2026-09-15g-aggtemp (ptownxf).",
               f"// Was: the recorded wrong behaviour of that row; now this fixture asserts the RIGHT answer."]
        if want == "pass":
            out = os.path.join(ROOT, "tests/logos/pass", fix + ".logos")
            exp = os.path.join(ROOT, "tests/logos/pass", fix + ".expected")
            open(out, "w").write("\n".join(hdr + prog) + "\n")
            open(exp, "w").write(f"exit: {res['run']}\nstdout: {res['out']}" if res["out"]
                                 else f"exit: {res['run']}\n")
        else:
            out = os.path.join(ROOT, "tests/logos/fail", fix + ".logos")
            exp = os.path.join(ROOT, "tests/logos/fail", fix + ".expected")
            open(out, "w").write("\n".join(hdr + prog) + "\n")
            open(exp, "w").write(res["diag"] + "\n")
        print(f"  wrote {out}")

main()
