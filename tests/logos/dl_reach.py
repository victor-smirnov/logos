#!/usr/bin/env python3
"""dl_reach.py — ASK THE BUILD, NOT THE TEXT, who can reach an llvm::DataLayout.

WHAT WAS WRONG WITH THE TEXT SCAN.  layout_engine_agreement_gate.sh asserted, by
`grep -rlnE '#include <llvm/IR/DataLayout.h>'` over src/compiler, that exactly two
translation units may see a DataLayout, and justified it:

    "A TU that does not include the header has no declaration to call, under any
     spelling, through any alias, behind any macro."

THAT IS FALSE, and the build says so.  `llvm/IR/Module.h` includes
`llvm/IR/DataLayout.h`, so every TU that includes Module.h holds a complete,
callable `llvm::DataLayout`.  MEASURED 2026-08-01 from `ninja -t deps` — the
compiler's OWN dependency record, i.e. what the preprocessor actually read —
NINE logosc TUs can name one, not two.  The scan was reporting a property of the
#include lines, and stating it as a property of the program.

A text scan of a source file cannot see what the preprocessor does.  So this
program asks two questions that the TOOLCHAIN answers:

  Q1  WHO CAN NAME ONE — from `ninja -t deps`, the `-MD` dependency list the
      compiler emitted for each object.  This is the preprocessed TU, not the
      first ten lines of the source.  Answer: an exact SET, compared in BOTH
      directions against a recorded one, so a shrink (the scan went blind) and a
      growth (a new TU pulled the header in) are both named.

  Q2  WHO ASKS ONE — from `nm --undefined-only` on the built objects: the
      undefined references to `llvm::DataLayout::*` / `llvm::StructLayout::*`
      that the linker will resolve.  This is the question the include check was
      TRYING to ask, and unlike the include check it also sees a NEW reader
      appearing INSIDE an already-allowed TU (the header's own stated blind
      spot).  Answer: an exact (TU -> symbol set) map, compared in both
      directions.

  ⚠ WHAT Q2 CANNOT SEE, MEASURED, NOT ASSUMED.  A probe TU compiled with this
  build's own flags shows `getTypeAllocSize`, `getTypeStoreSize` and
  `getTypeSizeInBits` leaving NO undefined symbol when the argument cannot be an
  aggregate: those are header-inline and bottom out in inline arithmetic.  The
  moment the argument can be a struct or a pointer they emit
  `llvm::DataLayout::getStructLayout` / `getPointerSpec`, which ARE out of line
  and DO appear.  So Q2 sees every AGGREGATE and POINTER query — which is the
  whole subject of layout disagreement, the leaf scalar table being shared by
  every engine since `8ba3c764` — and does not see a size query on a scalar type
  alone.  That residue is covered by the four-engine comparison inside the
  compiler, whose liveness the engine canaries prove in the same run.

CANARIES, both riding the SAME nm scan and the SAME classification:
  * a TU compiled HERE, from source, with this build's own flags, that calls
    `getStructLayout` — it must come back flagged as an out-of-bounds reader;
  * a planted copy of a real oracle object under a name that is not allowlisted
    — it must come back flagged as an unexpected TU.
A canary that is not caught makes this program exit 4 saying the INSTRUMENT is
broken.

EXIT CODES  0 clean · 1 the tree violates the recorded answer · 3 the question
could not be asked (no deps record, no objects, a partial build) · 4 a canary
was not caught.
"""
import argparse
import json
import os
import re
import shlex
import subprocess
import sys

SYM_RE = re.compile(r"(llvm|mlir)::(DataLayout|StructLayout)\b")
# A member whose presence means this TU ASKS A DATALAYOUT FOR A LAYOUT, as
# opposed to constructing/copying/destroying one or handing it to somebody else.
# Not a spelling guess: these are demangled symbol names, and the set of
# out-of-line members is fixed by the LLVM build, not by how the caller wrote
# the call. Anything not listed here still has to appear in the recorded map.
QUERY_MEMBERS = (
    "getStructLayout", "getABITypeAlign", "getPrefTypeAlign", "getPointerSpec",
    "getIndexedOffsetInType", "getTypeSizeInBits", "getTypeAllocSize",
    "getTypeStoreSize", "getPointerSizeInBits", "getPointerTypeSizeInBits",
    "getTypeABIAlignment", "getTypePreferredAlignment", "getTypeSize",
    "getTypeSizeInBits", "getTypeBitSize",
)


class Unaskable(Exception):
    """The question could not be put to the toolchain."""


def run(cmd, cwd=None):
    p = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)
    return p.returncode, p.stdout, p.stderr


def ninja_deps(build):
    """object -> [dependency paths], from the compiler's own -MD record."""
    if not os.path.exists(os.path.join(build, "build.ninja")):
        raise Unaskable(f"{build} has no build.ninja — this is not a build tree, "
                        f"so the compiler's dependency record cannot be read.")
    rc, out, err = run(["ninja", "-t", "deps"], cwd=build)
    if rc != 0 and not out:
        raise Unaskable(f"`ninja -t deps` failed in {build}: {err.strip()}")
    deps, cur = {}, None
    for line in out.splitlines():
        if not line:
            continue
        if not line[0].isspace():
            # "path/to.o: #deps N, deps mtime … (VALID|STALE)"
            obj = line.split(":", 1)[0]
            cur = deps.setdefault(obj, [])
            if "(STALE)" in line:
                cur = None      # a stale record is not an answer about this tree
            continue
        if cur is not None:
            cur.append(line.strip())
    if not deps:
        raise Unaskable(
            f"`ninja -t deps` in {build} returned no records at all. The "
            f"dependency log is empty, so 'nobody includes it' would be a "
            f"statement about nothing.")
    return deps


def target_tus(build, target_dir):
    """The BUILD's own statement of which sources make up a target — not a
    listing of *.cpp in a directory, which would also sweep up TUs that belong
    to other targets (src/compiler/trait_engine_test.cpp is one)."""
    ccj = os.path.join(build, "compile_commands.json")
    if not os.path.exists(ccj):
        raise Unaskable(f"no {ccj} — the target's source list can only be guessed.")
    tus = sorted({os.path.basename(e["file"])
                  for e in json.load(open(ccj))
                  if target_dir in e.get("output", "")})
    if not tus:
        raise Unaskable(f"no compile commands under {target_dir} in {ccj}.")
    return tus


def q1_include_reach(build, target_dir, header):
    """Which TUs of the target have `header` in their PREPROCESSED input."""
    deps = ninja_deps(build)
    mine = {os.path.basename(o.rstrip(":")).removesuffix(".o"): d
            for o, d in deps.items()
            if target_dir in o and d is not None}
    want = set(target_tus(build, target_dir))
    missing = want - set(mine)
    if missing:
        raise Unaskable(
            f"{len(missing)} of {len(want)} TUs of {target_dir} have no VALID "
            f"dependency record in {build}: {sorted(missing)[:8]}…\n"
            f"       The build is partial or stale, so an answer computed from it "
            f"would be about the TUs that happen to have been compiled.")
    return sorted(tu for tu, d in mine.items()
                  if any(p.endswith(header) for p in d))


def nm_refs(objects):
    """object path -> sorted undefined llvm::DataLayout/StructLayout symbols."""
    out = {}
    for obj in objects:
        rc, so, se = run(["nm", "-uC", obj])
        if rc != 0:
            raise Unaskable(f"nm failed on {obj}: {se.strip()}")
        syms = sorted({ln.split("U ", 1)[1].strip()
                       for ln in so.splitlines()
                       if " U " in ln and SYM_RE.search(ln)})
        if syms:
            out[os.path.basename(obj)] = syms
    return out


def objects_under(*dirs):
    objs = []
    for d in dirs:
        if not os.path.isdir(d):
            continue
        for root, _, files in os.walk(d):
            objs += [os.path.join(root, f) for f in sorted(files)
                     if f.endswith(".o")]
    return sorted(objs)


def is_query(sym):
    return any(m in sym for m in QUERY_MEMBERS)


def build_flags(build, model_tu):
    """THIS build's own flags for a model TU, minus -c/-o and the source."""
    ccj = os.path.join(build, "compile_commands.json")
    if not os.path.exists(ccj):
        raise Unaskable(f"no {ccj} (CMAKE_EXPORT_COMPILE_COMMANDS is ON in "
                        f"CMakeLists.txt:107) — the canary cannot be compiled the "
                        f"way the tree is, so the scan cannot be proven live.")
    entries = [e for e in json.load(open(ccj)) if e["file"].endswith(model_tu)]
    if not entries:
        raise Unaskable(f"no compile command for {model_tu} in {ccj}.")
    argv = shlex.split(entries[0]["command"])
    flags, i = [], 1
    while i < len(argv):
        t = argv[i]
        if t == "-o":
            i += 2; continue
        if t == "-c":
            i += 1; continue
        if t.endswith((".cpp", ".cc", ".cxx")):
            i += 1; continue
        flags.append(t); i += 1
    return argv[0], flags


def compile_canary(build, tmpd, model_tu):
    """Compile a TU that ASKS a DataLayout, with THIS build's own flags."""
    cxx, flags = build_flags(build, model_tu)
    src = os.path.join(tmpd, "planted_fifth_engine.cpp")
    with open(src, "w") as fh:
        fh.write(
            "// CANARY — a fifth engine asking BOTH DataLayouts for a layout.\n"
            "// The same nm scan that just said the tree is clean must flag it,\n"
            "// for both families: llvm::DataLayout (whose accumulation rule is\n"
            "// the one the object is emitted with) and mlir::DataLayout (whose\n"
            "// is NOT — it sums STORE sizes; {i56,i8,i64} is 16 against 24).\n"
            "#include <llvm/IR/Module.h>   // NOTE: not DataLayout.h — the point\n"
            "#include <llvm/IR/DerivedTypes.h>\n"
            "#include <mlir/Interfaces/DataLayoutInterfaces.h>\n"
            "uint64_t fifth_engine(const llvm::DataLayout& dl,\n"
            "                      llvm::StructType* st) {\n"
            "    return dl.getStructLayout(st)->getElementOffset(1);\n"
            "}\n"
            "uint64_t fifth_engine_mlir(const mlir::DataLayout& dl,\n"
            "                           mlir::Type t) {\n"
            "    return dl.getTypeSize(t);\n"
            "}\n")
    obj = os.path.join(tmpd, "planted_fifth_engine.cpp.o")
    rc, _, err = run([cxx] + flags + ["-c", src, "-o", obj], cwd=build)
    if rc != 0:
        raise Unaskable(f"the planted canary TU did not compile:\n{err[-2000:]}")
    return obj


def law_closure(build, tmpd, src_root, header, headers, extra_include=None,
                tag="law"):
    """THE LAW'S OWN INCLUDE CLOSURE, from the compiler.

    `grep -q DataLayout layout_law.hpp` answers a question about that ONE file's
    text. What matters is whether the law has a DataLayout IN SCOPE, which is a
    property of its whole preprocessed closure — the same mistake, one file
    smaller. So: compile a TU whose entire content is `#include "<header>"`,
    with this build's flags and `-MD`, and read the dependency list the compiler
    writes. Returns the closure's size and the offending members."""
    cxx, flags = build_flags(build, "mlir_gen_types.cpp")
    src = os.path.join(tmpd, f"{tag}_probe.cpp")
    with open(src, "w") as fh:
        if extra_include:
            fh.write(f"#include <{extra_include}>\n")
        fh.write(f'#include "{header}"\n')
    dfile = os.path.join(tmpd, f"{tag}_probe.d")
    obj = os.path.join(tmpd, f"{tag}_probe.o")
    rc, _, err = run([cxx] + flags + ["-I" + src_root, "-MD", "-MF", dfile,
                                      "-c", src, "-o", obj], cwd=build)
    if rc != 0:
        raise Unaskable(f"a TU containing only `#include \"{header}\"` did not "
                        f"compile, so the law's include closure could not be "
                        f"read:\n{err[-2000:]}")
    text = open(dfile).read().replace("\\\n", " ")
    paths = text.split(":", 1)[1].split()
    hit = sorted({h for h in headers for p in paths if p.endswith(h)})
    return len(paths), hit


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--build", required=True)
    ap.add_argument("--target-dir",
                    default="src/compiler/CMakeFiles/logosc.dir")
    ap.add_argument("--expected", required=True)
    ap.add_argument("--tmpd", required=True)
    ap.add_argument("--src", required=True,
                    help="src/compiler, for the law probe's -I")
    ap.add_argument("--law", default="layout_law.hpp")
    ap.add_argument("--header", default="llvm/IR/DataLayout.h")
    args = ap.parse_args(argv)

    try:
        exp = json.load(open(args.expected))
    except Exception as e:  # noqa: BLE001
        sys.stderr.write(f"FAIL (UNASKABLE): the recorded answer {args.expected} "
                         f"could not be read: {e}\n")
        return 3

    # ⚠ CANARIES ARE COUNTED, NOT DESCRIBED. The closing prose of this program
    # and of the gate that calls it named the canaries in a sentence somebody
    # maintained; MEASURED 2026-08-01, the gate's said NINE while TEN fired —
    # `declined`, added the day before, was caught and never listed. That is the
    # sixth recorded kind of lying gate (a measured claim nothing pins) inside
    # the artifact written to stop gates from lying. So every canary site that
    # did NOT go into `broken` appends its name here, the count travels to the
    # caller as a number, and no sentence restates it.
    bad, broken, caught = [], [], []
    try:
        # ── Q1: who can NAME one, per the compiler's dependency record ──────
        reach = q1_include_reach(args.build, args.target_dir, args.header)
        want = sorted(exp["include_reach"])
        if reach != want:
            bad.append(
                "Q1 (the preprocessor's own record): the set of logosc TUs whose\n"
                f"    preprocessed input contains {args.header} is\n"
                f"      {reach}\n"
                f"    and the recorded answer is\n"
                f"      {want}\n"
                f"    appeared: {sorted(set(reach) - set(want)) or '-'}\n"
                f"    vanished: {sorted(set(want) - set(reach)) or '-'}\n"
                "    A GROWTH is a new TU that can now name a DataLayout; a\n"
                "    SHRINK is this scan going blind. Both are events.")

        # ── Q2: who ASKS one, per the linker's view of the objects ──────────
        objdir = os.path.join(args.build, args.target_dir)
        objs = objects_under(objdir)
        if not objs:
            raise Unaskable(f"no objects under {objdir} — nothing was built, so "
                            f"'no TU asks a DataLayout' is about nothing.")
        refs = nm_refs(objs)
        exp_refs = {k: sorted(v) for k, v in exp["symbol_refs"].items()}
        if refs != exp_refs:
            for tu in sorted(set(refs) | set(exp_refs)):
                got, wnt = refs.get(tu, []), exp_refs.get(tu, [])
                if got == wnt:
                    continue
                bad.append(
                    f"Q2 (the linker's view): {tu} references\n"
                    f"      {got}\n"
                    f"    where the recorded answer is\n"
                    f"      {wnt}\n"
                    f"    new: {sorted(set(got) - set(wnt)) or '-'}   "
                    f"gone: {sorted(set(wnt) - set(got)) or '-'}")
        # …and the LOUD form of it: a query member outside the oracle TUs.
        oracles = set(exp["query_oracles"])
        for tu, syms in sorted(refs.items()):
            q = [s for s in syms if is_query(s)]
            if q and tu not in oracles:
                bad.append(
                    f"Q2: {tu} ASKS an llvm::DataLayout for a layout and is not a\n"
                    f"    layout oracle: {q}\n"
                    "    Only the oracle may; everything else asks layout_law.hpp /\n"
                    "    mlir_abi_size, which verify_layout_engines proves equal to\n"
                    "    llvm::DataLayout.")
        for tu in sorted(oracles):
            if not [s for s in refs.get(tu, []) if is_query(s)]:
                bad.append(
                    f"Q2: {tu} is recorded as a layout ORACLE and asks an\n"
                    f"    llvm::DataLayout NOTHING. Either it stopped being the\n"
                    f"    oracle, or this scan is looking at the wrong objects and\n"
                    f"    its 'nobody else asks one' is about nothing.")

        # ── THE LAW'S OWN CLOSURE, asked of the compiler ────────────────────
        # If layout_law.hpp could see a DataLayout, the one place that is
        # supposed to BE the answer would have a second answer in scope. That
        # was a `grep DataLayout layout_law.hpp` — a question about one file's
        # text, when what matters is the whole preprocessed closure.
        headers = ["llvm/IR/DataLayout.h",
                   "mlir/Interfaces/DataLayoutInterfaces.h"]
        n_law, law_hit = law_closure(args.build, args.tmpd, args.src, args.law,
                                     headers, tag="law")
        if law_hit:
            bad.append(
                f"THE LAW: {args.law}'s own include closure ({n_law} headers, per "
                f"the compiler's -MD record) contains {law_hit}.\n"
                "    The law is the place that IS the answer; a second answer in "
                "scope there is how five engines come to disagree again.")
        # …and the probe must be able to SEE one. Same compile, same -MD read,
        # with the header added: a probe that reports 'clean' for a TU that
        # demonstrably includes it is reading the wrong dependency file.
        n_c, law_canary = law_closure(args.build, args.tmpd, args.src, args.law,
                                      headers, extra_include=headers[0],
                                      tag="lawcanary")
        if not law_canary:
            broken.append(
                "CANARY 'the law's include closure' NOT CAUGHT: a probe TU that\n"
                f"    includes <{headers[0]}> BEFORE {args.law} came back with a\n"
                f"    {n_c}-header closure and no hit. The -MD record this arm\n"
                "    reads is not the one being written.")
        else:
            caught.append("the law's include closure (a probe TU with a "
                          "DataLayout header added)")

        # ── CANARY A: a TU compiled here, from source, that asks one ────────
        cobj = compile_canary(args.build, args.tmpd, "mlir_gen_types.cpp")
        crefs = nm_refs([cobj])
        cname = os.path.basename(cobj)
        cq = [s for s in crefs.get(cname, []) if is_query(s)]
        for fam in ("llvm::DataLayout", "mlir::DataLayout"):
            if not [s for s in cq if s.startswith(fam)]:
                broken.append(
                    f"CANARY 'a compiled fifth engine' NOT CAUGHT for {fam}: a TU\n"
                    "    compiled with this build's own flags, which reaches\n"
                    "    llvm::DataLayout only through <llvm/IR/Module.h> and asks\n"
                    "    both DataLayouts for a size, came back from the SAME nm\n"
                    f"    scan with {crefs.get(cname, [])!r}.\n"
                    "    The scan that just said the tree is clean cannot see a\n"
                    "    reader of that family.")
            else:
                caught.append(f"a fifth engine COMPILED from source, asking "
                              f"{fam}")
        # ── CANARY B: a planted copy of a real oracle object, misnamed ──────
        planted = os.path.join(args.tmpd, "planted_oracle_copy.cpp.o")
        model = os.path.join(objdir, "mlir_gen_types.cpp.o")
        with open(model, "rb") as a, open(planted, "wb") as b:
            b.write(a.read())
        prefs = nm_refs([planted])
        pq = [s for s in prefs.get(os.path.basename(planted), []) if is_query(s)]
        if not pq:
            broken.append(
                "CANARY 'a planted oracle copy' NOT CAUGHT: a byte-for-byte copy of\n"
                "    mlir_gen_types.cpp.o under a name that is not allowlisted came\n"
                "    back from the same scan with no query member. The scan is not\n"
                "    reading the objects it is pointed at.")
        else:
            caught.append("a planted byte-for-byte copy of an oracle object")
    except Unaskable as e:
        sys.stderr.write(f"FAIL (UNASKABLE — the build could not be asked): {e}\n")
        return 3

    if broken:
        sys.stderr.write("FAIL (dl_reach.py CANARY NOT CAUGHT): THE INSTRUMENT IS "
                         "BROKEN, not the tree.\n  " + "\n  ".join(broken) + "\n")
        return 4
    if bad:
        sys.stderr.write("FAIL (DataLayout reach):\n  " + "\n  ".join(bad) + "\n")
        return 1

    sys.stderr.write(
        f"[dl-reach] the law: {args.law}'s preprocessed closure is {n_law} headers "
        f"and contains no DataLayout of either kind; the same probe with one "
        f"added is flagged.\n"
        f"[dl-reach] Q1 the preprocessor's record: {len(reach)} logosc TUs can NAME "
        f"an llvm::DataLayout ({', '.join(reach)}) — the source-text scan this "
        f"replaces claimed 2.\n"
        f"[dl-reach] Q2 the linker's view: {len(refs)} TUs reference one; the "
        f"oracle(s) {sorted(oracles)} are the only ones that ASK it for a layout.\n"
        f"[dl-reach] {len(caught)} canaries caught: {'; '.join(caught)}\n")
    # The COUNT is a number the caller adds to its own tally, so the gate's
    # closing "N canaries caught" is arithmetic over sites that actually fired
    # and not a word somebody typed.
    sys.stderr.write("dl-reach-json: " + json.dumps({"canaries": len(caught)})
                     + "\n")
    print(json.dumps({"include_reach": reach, "symbol_refs": refs}, indent=1))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
