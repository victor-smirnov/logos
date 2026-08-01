#!/usr/bin/env python3
"""plan_dump_rules.py — judge the EMITTED plan dumps, and emit a VERDICT.

WHY THIS REPLACES THE SHELL RULES.

deferred_plan_gate.sh judged the emitted artifact with `sed` ranges, `awk` brace
walks and `grep -Eq PATTERN "${DUMPS[@]}"`, and one of those idioms is a
QUANTIFIER ERROR that reads as a pass:

    if ! grep -Eq 'self\\.order_ix == self\\.order_pick\\(…\\)' "${DUMPS[@]}"; then
        echo "FAIL: agrees does not go through order_pick — the two decision
               points can drift apart"

`grep` over MANY files is an EXISTENTIAL.  The sentence is a UNIVERSAL: EVERY
emitted plan re-derives the prepared decision through the one cost function.
MEASURED 2026-08-01: rewriting TWO of the THREE emitted `agrees` bodies to
compare against a hand-written `if (fresh.base_n < fresh.step_n) …` — precisely
the drift the rule exists to forbid — left the gate at EXIT 0, printing
"OK: deferred plan — guarded blocks ran on 3/1/1/2/3 dumps".  One surviving dump
satisfied the ∃ and the ∀ was never asked.

So the rules move here, where a rule is a loop over the dumps that CARRY the
artifact and a violation is a NAMED ROW.  Nothing is a substring test over a
concatenation of files.  A body is extracted by BRACE MATCHING from its `pub fn`
line, not by a `sed` range ending at the first `^    }$` — and a body that
cannot be extracted is a VIOLATION, never an empty string that satisfies a
negative test.

THE VERDICT is one JSON line, `plan-rules-json:`, read by tests/logos/verdict.py
with a strict parser: a renamed field is fatal there, not an empty variable.

SELF-PROOF.  The gate runs this program TWICE in the same invocation: over the
real dumps, where `violations` must be 0, and over a dump deliberately broken
five ways, where every one of the five must come back NAMED.  Same extraction,
same rules, same verdict document.
"""
import argparse
import json
import os
import re
import sys

IMPL_RE = re.compile(r"^impl ([A-Za-z_][A-Za-z0-9_]*Plan) \{")
FN_RE = re.compile(r"^    pub fn ([A-Za-z_][A-Za-z0-9_]*)\(")
LOOP_RE = re.compile(r"^[ \t]*(while|loop|for)[ \t(]")
ORDER_PICK_RE = re.compile(
    r"self\.order_ix == self\.order_pick\(fresh\.base_n, fresh\.step_n, "
    r"fresh\.n2, fresh\.n3\)")

# The members a plan type must carry for the deferred half to be distinguishable
# from a refusal. Checked on EVERY dump that carries an `impl …Plan {`.
REQUIRED_MEMBERS = [
    ("pub defer_order: bool", "the deferred flag itself"),
    ("pub fn settled(&self) -> bool", "the accessor that discloses it"),
    ("pub fn order_pick(&self, n0: i64, n1: i64, n2: i64, n3: i64) -> i64",
     "the ONE cost function both decision points go through"),
]


def fn_bodies(lines):
    """{name: (start_line, [body lines])} by BRACE MATCHING.

    A `sed -n '/^    pub fn margin(/,/^    }$/p'` range stops at the first line
    that is exactly four spaces and a brace, which is a guess about formatting,
    and returns NOTHING when the fn is renamed — an empty string that every
    `grep -q` negative then passes. Here a missing body is a fact the caller
    must handle, and an inner block cannot end the extraction early."""
    out, i, n = {}, 0, len(lines)
    while i < n:
        m = FN_RE.match(lines[i])
        if not m:
            i += 1
            continue
        depth, j, body = 0, i, []
        started = False
        while j < n:
            body.append(lines[j])
            depth += lines[j].count("{") - lines[j].count("}")
            if "{" in lines[j]:
                started = True
            if started and depth <= 0:
                break
            j += 1
        out[m.group(1)] = (i + 1, body)
        i = j + 1
    return out


def loop_nesting_hits(lines, needle="__defer"):
    """Lines mentioning `needle` at loop depth > 0, by a brace walk.

    Measured by BRACES, not by column: a four-nest chain's later tests sit at
    indents 8 and 12 while still standing above every loop (ADR 0024 S4m), and a
    read at indent 4 INSIDE a `while` body whose brace opened earlier is
    invisible to a column rule."""
    hits, stack, loopdepth, pending = [], [], 0, False
    for lineno, line in enumerate(lines, 1):
        if LOOP_RE.match(line):
            pending = True
        if loopdepth > 0 and needle in line:
            hits.append(f"{lineno}: {line.strip()}")
        opens, closes = line.count("{"), line.count("}")
        for k in range(opens):
            is_loop = pending and k == 0
            stack.append(is_loop)
            if is_loop:
                loopdepth += 1
            pending = False
        for _ in range(closes):
            if stack:
                if stack.pop():
                    loopdepth -= 1
        pending = False
    return hits


def judge(path):
    """-> (violations, per-file counts). Every rule is UNIVERSAL over the dumps
    that carry the artifact it is about."""
    v = []
    lines = open(path, errors="replace").read().splitlines()
    base = os.path.basename(path)
    c = dict(impl=0, agrees=0, margin=0, defer_ix=0, prelude_n0=0, prelude_n1=0,
             deferred_prepare=0, via_rel3=0, via_rel=0, rel_read=0,
             defer_ix_pick=0, defer_ix_test4=0, r3_chain=0, r3_nested=0)

    has_impl = any(IMPL_RE.match(ln) for ln in lines)
    bodies = fn_bodies(lines)

    if has_impl:
        c["impl"] = 1
        for member, what in REQUIRED_MEMBERS:
            if not any(member in ln for ln in lines):
                v.append(f"{base}: the plan type has no `{member}` ({what}) — the "
                         f"deferred half would be indistinguishable from a refusal")
        # ── agrees: EVERY emitted plan, not SOME ────────────────────────────
        if "agrees" not in bodies:
            v.append(f"{base}: carries an `impl …Plan` block and NO `agrees` body "
                     f"could be extracted — the rules below judged NOTHING")
        else:
            ln0, body = bodies["agrees"]
            c["agrees"] = 1
            text = "\n".join(body)
            if "defer_order" in text:
                v.append(f"{base}:{ln0}: agrees consults defer_order — it would "
                         f"claim to cover a half that is re-decided every call")
            if "if (!self.dyn_order) {" not in text:
                v.append(f"{base}:{ln0}: agrees does not gate on dyn_order — it no "
                         f"longer answers about the prepared half alone")
            if not ORDER_PICK_RE.search(text):
                v.append(f"{base}:{ln0}: agrees does not re-derive the decision "
                         f"through `order_pick` — a second hand-written comparison "
                         f"is how the two decision points come to disagree. "
                         f"⚠ THIS RULE WAS AN EXISTENTIAL `grep` OVER ALL DUMPS "
                         f"AND WAS GREEN WITH TWO OF THREE PLANS VIOLATING IT.")
        if "margin" not in bodies:
            v.append(f"{base}: carries an `impl …Plan` block and NO `margin` body "
                     f"could be extracted — the rule below judged NOTHING")
        else:
            ln0, body = bodies["margin"]
            c["margin"] = 1
            if "defer_order" in "\n".join(body):
                v.append(f"{base}:{ln0}: margin consults defer_order — a per-call "
                         f"decision has no distance to a flip")

    # ── the per-row cost: no `__defer` read inside a loop, in ANY dump ──────
    for hit in loop_nesting_hits(lines):
        v.append(f"{base}: a `__defer` read or the discriminant is INSIDE a loop "
                 f"— {hit}")

    for ln in lines:
        if ln.startswith("    let __defer_ix: i64"):
            c["defer_ix"] += 1
        if ln.startswith("    let __defer_n0: i64 ="):
            c["prelude_n0"] += 1
        if ln.startswith("    let __defer_n1: i64 ="):
            c["prelude_n1"] += 1
        if "let __defer_n0: i64 = (__rel_hot_sl).len();" in ln:
            c["rel_read"] += 1
        if ln == ("    let __defer_ix: i64 = __pl.order_pick(__defer_n0, "
                  "__defer_n1, __defer_n2, __defer_n3);"):
            c["defer_ix_pick"] += 1
        if ln == "    if ((__defer_ix == 1i64)) {":
            c["defer_ix_test4"] += 1
    if re.search(r"^pub fn via_rel3_run\(", "\n".join(lines), re.M):
        c["via_rel3"] = 1
        # FOUR nests, so THREE tests, and at least one of them past indent 4 —
        # the artifact the loop-nesting rule had to be rewritten for (S4m): a
        # column rule would reject this shape, and the corpus had no such query
        # for it to bite on until `via_rel3`.
        chain = [ln for ln in lines
                 if re.match(r"^ +if \(\(__defer_ix == [0-9]+i64\)\) \{$", ln)]
        nested = [ln for ln in chain
                  if re.match(r"^ {5,}if ", ln)]
        c["r3_chain"] = len(chain)
        c["r3_nested"] = len(nested)
        if len(chain) != 3:
            v.append(f"{base}: {len(chain)} order tests in via_rel3_run (want 3: "
                     f"candidates 1..3, with 0 as the final else)")
        if not nested:
            v.append(f"{base}: via_rel3_run has no branch past indent 4 — the "
                     f"artifact the loop-nesting rule exists for is gone")
    if re.search(r"^pub fn via_rel_run\(", "\n".join(lines), re.M):
        c["via_rel"] = 1
        # `ls` is 9 rows in both data sets while the rel is 2 and 7, so the wrong
        # read is invisible on the mirror case: pinned on the text instead.
        for lineno, ln in enumerate(lines, 1):
            if re.match(r"^    let __defer_n[01]: i64 = \(ls\)\.len\(\);", ln):
                v.append(f"{base}:{lineno}: via_rel's deferred size reads the input "
                         f"parameter instead of the materialized rel")
    if re.search(r"^pub fn (via_rel|via_rel3|iter_step)_prepare\(",
                 "\n".join(lines), re.M):
        c["deferred_prepare"] = 1
        if any(LOOP_RE.match(ln) for ln in lines):
            v.append(f"{base}: a deferred prepare contains a loop — it would run "
                     f"the query it declines to plan")
        if any(re.match(r"^    let __n[01]: i64 = ", ln) for ln in lines):
            v.append(f"{base}: a deferred prepare measures a size it will not "
                     f"compare")
        if not any("defer_order: true" in ln for ln in lines):
            v.append(f"{base}: a deferred prepare does not record that the order "
                     f"is left to run")
    return v, c


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("dumps", nargs="+")
    ap.add_argument("--label", default="dumps")
    args = ap.parse_args(argv)

    missing = [d for d in args.dumps if not os.path.isfile(d)]
    if missing:
        sys.stderr.write(f"plan-rules: {missing} do not exist — nothing to judge, "
                         f"and 'no violations' would be about nothing.\n")
        return 3

    all_v, tot = [], dict(dumps=0)
    for d in sorted(args.dumps):
        v, c = judge(d)
        all_v += v
        tot["dumps"] += 1
        for k, n in c.items():
            tot[k] = tot.get(k, 0) + n
    tot["violations"] = len(all_v)
    doc = dict(tot)
    doc["label"] = args.label
    sys.stderr.write("plan-rules-json: " + json.dumps(doc, sort_keys=True) + "\n")
    for row in all_v:
        sys.stderr.write("plan-rules-violation: " + row + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
