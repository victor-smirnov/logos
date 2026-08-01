#!/usr/bin/env python3
"""verdict.py — read a STRUCTURED verdict and assert on it, FATALLY.

WHY THIS EXISTS.  A gate written in shell reads its subject with `sed`, `grep -c`
and `[ "$X" -lt N ]`, and every one of those has a failure mode that is
INDISTINGUISHABLE FROM SUCCESS:

  * `sed -E 's/… ([0-9]+) defs …/\\1/'` on a NON-MATCHING line prints the WHOLE
    LINE.  The variable is then a sentence, not a number.
  * `[ "<a sentence>" -lt 3676 ]` writes "integer expression expected" to stderr
    and exits 2 — and `if` reads any non-zero status as FALSE, so the floor
    never fires.  MEASURED 2026-08-01: a wrapper that rewrote only `, N defs,`
    to `, defs=N,` on logosc's stderr left layout_engine_agreement_gate.sh at
    EXIT 0, printing its OK line with the whole census line embedded in it.
  * `grep -c` yields 0, which is a number, so a floor of 0 reads as a
    measurement.
  * `grep -q PATTERN file1 file2 file3` is an EXISTENTIAL.  A rule that means
    "every emitted plan goes through order_pick" written that way is green when
    two of three do not.  MEASURED 2026-08-01: deferred_plan_gate.sh EXIT 0.

The fix is not another floor.  It is to change the MEDIUM: the subject emits a
verdict with NAMED, TYPED fields, and this program reads it, where a missing
field, a renamed field, a non-integer value or a malformed document is a FATAL
ERROR that NAMES WHAT IT COULD NOT PARSE.  There is no code path here that turns
"I could not read that" into "nothing is wrong".

CONTRACT
  verdict.py --file F --prefix P [options]
    F        a file the subject wrote (usually captured stderr)
    P        the line prefix that introduces the verdict; the rest of the line
             must be ONE JSON object.  EXACTLY ONE line may carry it.

  --exact-keys a,b,c   the top-level key set must be EXACTLY this.  A renamed or
                       added field is then fatal even if nothing floors it.
  --bind NAME=path     resolve a dotted path to an int and bind it to a shell
                       variable NAME.
  --floor PATH N       the value at `PATH` must be >= N.
  --eq    PATH N       the value at `PATH` must be == N.
  --export FILE        write `NAME=value` lines for every --bind.  Written ONLY
                       if every bind resolved and every assertion passed, so a
                       gate that `source`s it under `set -u` cannot proceed on a
                       partial read.
  --label TEXT         what the subject is, for the messages.

EXIT CODES
  0  every assertion held
  1  an assertion FAILED (a floor, an equality) — the TREE is what is wrong
  3  the verdict could not be READ (absent, duplicated, malformed, a missing or
     mistyped field, an unknown key) — THE INSTRUMENT is what is wrong
  4  --selftest found this parser accepting something it must reject — THIS
     PROGRAM is what is wrong

GATE CENSUS — WHAT EACH GATE PARSED BEFORE THIS, AND WHAT IT PARSES NOW
(2026-08-01. "scrape" = a `sed` substitution whose non-match returns its input,
or a `[ "$X" -lt N ]` on something that might not be a number; "∃/∀" = a
`grep -q PAT file1 file2 …` standing in for a sentence about EVERY file.)

  layout_engine_agreement_gate.sh
    BEFORE  6 `sed` reads of one prose census line (3 of them the unsafe idiom,
            3 the safe `;t;s/.*/0/` one — the idiom was known and applied to
            half the parses); 18 matrix cells scraped one `sed -nE` each; the
            generator counts by `sed -nE …p`; a `grep` over #include LINES for
            "who can see a DataLayout"; a `grep` of layout_law.hpp's own text; a
            `grep -rn` for the spelling `mlir::DataLayout`.
    AFTER   ONE `layout-verify-json:` object read by verdict.py (--exact-keys,
            --bind, --floor on `matrix.<engine>.<shape>`); `lattice-gen-json:`
            and `oracle-gen-json:` likewise, the latter with `--eq canary 0/1`;
            the DataLayout question put to the BUILD by dl_reach.py (`ninja -t
            deps` for who can NAME one, `nm -uC` for who ASKS one, an -MD probe
            for the law's own include closure), compared in both directions
            against a recorded answer with per-TU grounds.

  deferred_plan_gate.sh
    BEFORE  `sed` range extraction of `agrees`/`margin` bodies (empty on a
            rename, and an empty body passes every negative test); an `awk`
            brace walk; `grep -Ec | awk` counts; and TWO ∀ sentences written as
            ∃ over the dump set — one of them the order_pick rule, GREEN with
            two of three plans violating it.
    AFTER   plan_dump_rules.py: brace-matched body extraction, every rule a loop
            over the dumps that CARRY the artifact, violations as named rows,
            the whole census one `plan-rules-json:` object read by verdict.py
            (`--eq violations 0`). Canary: one dump broken FIVE ways through the
            same program. Residual shell: the `[plan]` prose trace (below).

  prepared_plan_gate.sh, join_order_multi_gate.sh
    BEFORE  the same ∃-for-∀ member/delegation rules.
    AFTER   universal over the dumps that carry a plan impl, with the admitting
            count floored. join_order_multi's rule had to STATE ITS CONDITION to
            become universal: `Q5Plan` declines the reorder and carries no
            candidate table, so the delegation rule is about plans that carry
            one — which the ∃ form could neither notice nor say.

  no_dup_use_gate.sh
    BEFORE  a canary compiled from a DIFFERENT three-line source WITHOUT the
            `--gen-dir` flag, certifying the silence of a `--gen-dir` compile.
    AFTER   the canary is the FIXTURE ITSELF with one `use` duplicated, compiled
            with the identical argv. Counts unchanged (`grep -c`, which fails
            closed at 0 against a positive floor).

  NOT CONVERTED, WITH GROUND — each looked at, each left, each said out loud:
    corpus_registration_gate.sh  `wc -l`, `grep -c .` and set differences over
      file LISTS. A count of lines in a file this gate wrote itself; no parse of
      a subject's prose, no non-match path that yields a number.
    flat_body_gate.sh, plan_size_gate.sh  `awk … END{print c+0}` — always a
      number, floored positive, so a dead pattern reads as 0 and fires.
    join_order_gate.sh, join_order_key_fidelity_gate.sh  `grep -Fc|awk` sums and
      `case` on an extracted literal whose empty value falls to the failing arm.
      Their remaining `grep -Fq` over the dump set are PRESENCE assertions about
      one named artifact (a specific plan type, a specific emitted line), where
      ∃ is the sentence, not a weakening of it.
    plan_independence_gate.sh, why_size_gate.sh, wql_shadowed_column_gate.sh,
      run_gendir_test.sh, render_type_fidelity.sh  counts from `grep -c`/`${#a[@]}`
      /`size|awk` against positive floors, and single-file matches. Fail closed.
    test-levels.sh  one `sed -E 's/[_-].*//'` on a basename where returning the
      whole basename IS the intended answer for a token with no separator; the
      rest is `grep -c .` and arithmetic. Its `tot_run < COUNT` check is the
      floor that catches a suite that silently shrank.
    ctest-summary.sh, perf-slow.sh  reporting tools, not gates: they pronounce
      no verdict.
    scripts/abi-check.sh  reads a TAB-SEPARATED spec it generates itself with
      `grep -c "^<kind>\t"`, floors each kind, and canaries the differ in both
      directions. The subject is already a structured record.
    THE `[plan]` TRACE CHANNEL (deferred/prepared/join-order gates)  ~20 rules
      assert SENTENCES of the compiler's justification prose. That prose IS the
      artifact under test — it is what a human reads to know why a plan was
      chosen — so asserting its text is the point, not a workaround. Each rule
      fails on absence, and one floor on the channel's line count says "the
      channel spoke" once instead of twenty misleading times. A JSON mirror here
      would be a second spelling of the same absence.

AND THIS PROGRAM CHECKS ITS OWN MEDIUM.  `verdict.py --selftest` pushes a table
of deliberately broken inputs through `read_verdict`/`assertions` — the SAME
functions the real read uses, in the same process — and requires each to be
rejected with the expected reason.  If one is accepted, it exits 4 saying THE
PARSER IS BROKEN.  Gates call `--selftest` in the same invocation as the real
read, so "the verdict parsed" is never a statement about an untested reader.
"""
import argparse
import json
import os
import sys


class Unreadable(Exception):
    """The verdict could not be read. NEVER caught to produce a pass."""


class Failed(Exception):
    """The verdict was read and an assertion did not hold."""


def read_verdict(text, prefix, label="the subject"):
    """text -> dict. Raises Unreadable, naming what could not be parsed."""
    hits = [ln for ln in text.splitlines() if ln.startswith(prefix)]
    if not hits:
        raise Unreadable(
            f"{label}: NOT ONE line begins with {prefix!r}.\n"
            f"       The verdict this gate reads was never emitted, so every\n"
            f"       floor below would be a statement about nothing. A gate that\n"
            f"       could not look must not report that nothing is wrong.")
    if len(hits) > 1:
        raise Unreadable(
            f"{label}: {len(hits)} lines begin with {prefix!r}, want exactly 1.\n"
            f"       Which one is the verdict is then a guess, and a guess that\n"
            f"       silently takes the first is how a stale run gets read as a\n"
            f"       fresh one.")
    body = hits[0][len(prefix):].strip()
    if not body:
        raise Unreadable(f"{label}: the {prefix!r} line carries no document at all.")
    try:
        doc = json.loads(body)
    except json.JSONDecodeError as e:
        raise Unreadable(
            f"{label}: the {prefix!r} line is not valid JSON: {e}.\n"
            f"       line was: {body[:300]}")
    if not isinstance(doc, dict):
        raise Unreadable(
            f"{label}: the verdict is a {type(doc).__name__}, not an object.")
    return doc


def resolve_int(doc, path, label="the subject"):
    """Dotted path -> int. Raises Unreadable naming the path AND what IS there."""
    cur = doc
    walked = []
    for part in path.split("."):
        if not isinstance(cur, dict):
            raise Unreadable(
                f"{label}: {'.'.join(walked) or '<root>'} is a "
                f"{type(cur).__name__}, so {path!r} cannot be resolved through it.")
        if part not in cur:
            raise Unreadable(
                f"{label}: no field {path!r} in the verdict — it stops at "
                f"{'.'.join(walked) or '<root>'}, which has "
                f"{sorted(cur.keys())}.\n"
                f"       A field this gate floors was RENAMED or DELETED. The old\n"
                f"       shell read would have taken the whole line as the value\n"
                f"       and the floor would have silently passed.")
        walked.append(part)
        cur = cur[part]
    if isinstance(cur, bool) or not isinstance(cur, int):
        raise Unreadable(
            f"{label}: field {path!r} is {cur!r} ({type(cur).__name__}), not an "
            f"integer. A floor on a non-number is not a measurement.")
    return cur


def resolve_int_default(doc, path, default, label="the subject"):
    """Like resolve_int, but a MISSING LEAF is the given default.

    For a field that is legitimately absent — `engines.sema_abi_layout` is not
    emitted when that engine recorded nothing, and absent must read as 0. ONLY
    the leaf may be missing: if a PARENT is missing, or the leaf is present but
    not an integer, that is still fatal, because those are shape drift and not
    the documented absence."""
    parts = path.split(".")
    cur = doc
    for i, part in enumerate(parts[:-1]):
        if not isinstance(cur, dict) or part not in cur:
            raise Unreadable(
                f"{label}: {'.'.join(parts[:i + 1])!r} — the CONTAINER of the "
                f"optional field {path!r} is missing. Only the leaf may be "
                f"absent; a missing container is the verdict's shape moving.")
        cur = cur[part]
    if not isinstance(cur, dict):
        raise Unreadable(
            f"{label}: {'.'.join(parts[:-1]) or '<root>'} is a "
            f"{type(cur).__name__}, not an object.")
    if parts[-1] not in cur:
        return default
    v = cur[parts[-1]]
    if isinstance(v, bool) or not isinstance(v, int):
        raise Unreadable(
            f"{label}: optional field {path!r} is present but is {v!r} "
            f"({type(v).__name__}), not an integer.")
    return v


def check_exact_keys(doc, want, label="the subject"):
    have = set(doc.keys())
    want = set(want)
    if have != want:
        raise Unreadable(
            f"{label}: the verdict's top-level fields are {sorted(have)};\n"
            f"       this gate was written against {sorted(want)}.\n"
            f"       missing: {sorted(want - have) or '-'}   "
            f"unexpected: {sorted(have - want) or '-'}\n"
            f"       A verdict whose shape moved is a verdict this gate does not\n"
            f"       know how to read. Update the gate deliberately.")


def assertions(doc, floors, eqs, label="the subject"):
    """Raises Unreadable (cannot read) or Failed (read, and wrong)."""
    bad = []
    for path, want in floors:
        got = resolve_int(doc, path, label)
        if got < want:
            bad.append(f"{label}: {path} — observed {got}, floor {want}.\n"
                       f"       A floor here is a value this gate actually measured,\n"
                       f"       not a fraction of it. If the drop is deliberate, edit\n"
                       f"       the floor and put its ground in the commit message.")
    for path, want in eqs:
        got = resolve_int(doc, path, label)
        if got != want:
            bad.append(f"{label}: {path} — observed {got}, want exactly {want}.")
    if bad:
        raise Failed("\n".join(bad))


# ── the parser's own medium, proved the way the canaries are proved ──────────
# Each row is (name, thunk, substring the rejection must contain). A row that
# does NOT raise is a hole in this program, and `--selftest` exits 4 for it.
def _selftest_cases():
    good = 'V: {"a": 3, "b": {"c": 4}}'
    return [
        ("the verdict line is absent",
         lambda: read_verdict("nothing here\n", "V: "), "NOT ONE line"),
        ("the verdict line appears twice",
         lambda: read_verdict(good + "\n" + good + "\n", "V: "), "want exactly 1"),
        ("the verdict line is empty",
         lambda: read_verdict("V: \n", "V: "), "no document"),
        ("the document is malformed JSON",
         lambda: read_verdict('V: {"a": 3,}\n', "V: "), "not valid JSON"),
        ("the document is truncated",
         lambda: read_verdict('V: {"a": 3\n', "V: "), "not valid JSON"),
        ("the document is not an object",
         lambda: read_verdict('V: [1,2,3]\n', "V: "), "not an object"),
        ("a floored field was RENAMED",
         lambda: resolve_int(read_verdict(good, "V: "), "z"), "no field 'z'"),
        ("a floored field was renamed one level down",
         lambda: resolve_int(read_verdict(good, "V: "), "b.z"), "no field 'b.z'"),
        ("a floored field became a string",
         lambda: resolve_int(read_verdict('V: {"a": "3676 struct types"}', "V: "),
                             "a"), "not an integer"),
        ("a floored field became a bool",
         lambda: resolve_int(read_verdict('V: {"a": true}', "V: "), "a"),
         "not an integer"),
        ("a floored field became null",
         lambda: resolve_int(read_verdict('V: {"a": null}', "V: "), "a"),
         "not an integer"),
        ("a path is walked through a scalar",
         lambda: resolve_int(read_verdict(good, "V: "), "a.b"), "cannot be resolved"),
        ("an optional field's CONTAINER is missing",
         lambda: resolve_int_default(read_verdict(good, "V: "), "z.c", 0),
         "CONTAINER of the optional field"),
        ("an optional field is present but is a string",
         lambda: resolve_int_default(read_verdict('V: {"b": {"c": "x"}}', "V: "),
                                     "b.c", 0), "not an integer"),
        ("the top-level key set drifted",
         lambda: check_exact_keys(read_verdict(good, "V: "), ["a", "b", "c"]),
         "missing: ['c']"),
        ("the top-level key set grew",
         lambda: check_exact_keys(read_verdict(good, "V: "), ["a"]),
         "unexpected: ['b']"),
    ]


def _selftest_failing_cases():
    doc = {"a": 3, "b": {"c": 4}}
    return [
        ("a floor is not met", lambda: assertions(doc, [("a", 4)], [])),
        ("a nested floor is not met", lambda: assertions(doc, [("b.c", 9)], [])),
        ("an equality does not hold", lambda: assertions(doc, [], [("a", 7)])),
    ]


def selftest():
    broken = []
    for name, thunk, want in _selftest_cases():
        try:
            thunk()
        except Unreadable as e:
            if want not in str(e):
                broken.append(f"  {name}: rejected, but the reason did not contain "
                              f"{want!r} — it said: {e}")
            continue
        except Exception as e:   # noqa: BLE001 — any other escape is also a hole
            broken.append(f"  {name}: raised {type(e).__name__}, not Unreadable: {e}")
            continue
        broken.append(f"  {name}: ACCEPTED. verdict.py would have read this as a "
                      f"good verdict.")
    for name, thunk in _selftest_failing_cases():
        try:
            thunk()
        except Failed:
            continue
        except Exception as e:   # noqa: BLE001
            broken.append(f"  {name}: raised {type(e).__name__}, not Failed: {e}")
            continue
        broken.append(f"  {name}: ACCEPTED. verdict.py would have called this a pass.")
    # And the happy path must still pass, or every rejection above is trivially
    # met by a parser that rejects everything.
    try:
        d = read_verdict('noise\nV: {"a": 3, "b": {"c": 4}}\nmore noise\n', "V: ")
        check_exact_keys(d, ["a", "b"])
        assertions(d, [("a", 3), ("b.c", 4)], [("a", 3)])
        if resolve_int(d, "b.c") != 4:
            broken.append("  the happy path: b.c did not resolve to 4")
        if resolve_int_default(d, "b.absent", 7) != 7:
            broken.append("  the happy path: an absent leaf did not take its default")
        if resolve_int_default(d, "b.c", 7) != 4:
            broken.append("  the happy path: a present leaf took the default anyway")
    except Exception as e:   # noqa: BLE001
        broken.append(f"  the happy path was REJECTED ({type(e).__name__}: {e}) — a "
                      f"parser that rejects everything proves nothing above.")
    if broken:
        sys.stderr.write(
            "FAIL (verdict.py SELFTEST): this parser does not reject inputs it "
            "must reject.\n"
            "      THE INSTRUMENT THAT READS EVERY OTHER VERDICT IS BROKEN.\n"
            + "\n".join(broken) + "\n")
        return 4
    n = len(_selftest_cases()) + len(_selftest_failing_cases())
    sys.stderr.write(
        f"[verdict] selftest: {n} malformed/false inputs pushed through the same "
        f"read_verdict/resolve_int/assertions this run uses — every one rejected, "
        f"and the well-formed one accepted.\n")
    return 0


def _kv(s, sep, what):
    if sep not in s:
        sys.exit(f"verdict.py: --{what} wants NAME{sep}VALUE, got {s!r}")
    a, b = s.rsplit(sep, 1)
    return a, b


def main(argv):
    ap = argparse.ArgumentParser(add_help=True)
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--file")
    ap.add_argument("--prefix")
    ap.add_argument("--label", default="the subject")
    ap.add_argument("--exact-keys", default=None)
    ap.add_argument("--bind", action="append", default=[])
    ap.add_argument("--bind-opt", action="append", default=[],
                    help="NAME=path:DEFAULT — a leaf that may legitimately be "
                         "absent; a missing PARENT is still fatal")
    ap.add_argument("--floor", action="append", nargs=2, default=[],
                    metavar=("PATH", "N"))
    ap.add_argument("--eq", action="append", nargs=2, default=[],
                    metavar=("PATH", "N"))
    ap.add_argument("--export")
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args(argv)

    if args.selftest:
        rc = selftest()
        if rc or not args.file:
            return rc

    if not args.file or not args.prefix:
        sys.exit("verdict.py: --file and --prefix are required")

    label = args.label
    try:
        if not os.path.exists(args.file):
            raise Unreadable(f"{label}: {args.file} does not exist — the subject "
                             f"wrote nothing for this gate to read.")
        with open(args.file, "r", errors="replace") as fh:
            text = fh.read()
        doc = read_verdict(text, args.prefix, label)
        if args.exact_keys is not None:
            check_exact_keys(doc, [k for k in args.exact_keys.split(",") if k], label)
        floors = [(p, int(v)) for p, v in args.floor]
        eqs = [(p, int(v)) for p, v in args.eq]
        binds = [_kv(b, "=", "bind") for b in args.bind]
        # Resolve EVERY bind before asserting anything: a rename must be reported
        # as a rename, not as whatever floor happens to be listed first.
        values = {name: resolve_int(doc, path, label) for name, path in binds}
        for spec in args.bind_opt:
            name, rest = _kv(spec, "=", "bind-opt")
            path, dflt = _kv(rest, ":", "bind-opt")
            values[name] = resolve_int_default(doc, path, int(dflt), label)
        assertions(doc, floors, eqs, label)
    except Unreadable as e:
        sys.stderr.write(f"FAIL (VERDICT UNREADABLE): {e}\n")
        return 3
    except Failed as e:
        sys.stderr.write(f"FAIL: {e}\n")
        return 1

    if args.export:
        # Written only now — after every bind resolved and every assertion held.
        # A gate that sources this under `set -u` therefore either has every
        # variable or dies naming the one it does not have.
        with open(args.export, "w") as fh:
            for name, val in values.items():
                fh.write(f"{name}={val}\n")
    if not args.quiet and values:
        sys.stderr.write("[verdict] " + label + ": " +
                         ", ".join(f"{k}={v}" for k, v in values.items()) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
