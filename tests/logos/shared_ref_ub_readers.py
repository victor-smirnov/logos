#!/usr/bin/env python3
"""Readers for shared_ref_ub_lint.sh — the parts that cannot be done with sed.

Every function here is used BOTH on the tree and on a planted canary by the
driver script. None of them decides anything; they MEASURE, and the driver
holds the measurement against a ledger.

Three readers, one per thing the old sed-based lint got wrong:

  guard      — CONTAINMENT under `type_is_freeze`, by brace matching, not by
               matching the literal text `if (type_is_freeze(`. The old reader
               went red when the predicate was hoisted into a named bool — a
               semantics-identical refactor — and a lint that cries wolf on an
               honest edit is deleted, which is the same defect one level up.

  claims     — the load-bearing CLAIM SENTENCES, delimited in the C++ source by
               `//@claim <id>` … `//@endclaim`, hashed one block at a time. The
               old lint checked that a rule-id STRING was present, so pasting
               the false DEFERRED bullet back over the whole settled-rule
               comment left it exiting 0. Prose outside a claim block stays free
               to improve; a claim cannot be reversed silently.

  interior   — the INTERIOR-MUTABILITY ROOTS over ALL of stdlib, not one file.
               The old census read stdlib/lang/atomic/atomic.logos alone and
               printed its 12 rows as though they were the population; Mutex,
               RwLock, Cell, RefCell, OnceCell, UnsafeCell and the Writ arena
               were invisible to it, and stripping UnsafeCell out of Mutex/RwLock
               left it exiting 0.

  transitive — the types that are non-Freeze only THROUGH a root, computed with
               the SAME indirection stop `type_is_freeze` uses (a field behind
               `*`/`&`/`fn` does not infect its container). A syntactic closure
               without that stop would report Rc/Arc as interior-mutable, which
               is exactly backwards.
"""

import hashlib
import os
import re
import sys

# ── C++ source hygiene ───────────────────────────────────────────────────────


def strip_cpp_comments(text: str) -> str:
    """Blank out // and /* */ comments, PRESERVING every byte offset.

    Offsets are preserved so brace matching on the stripped text can be mapped
    straight back onto the original. String and char literals are respected —
    `"logos.lang.cell"` must not be mistaken for anything, and a `//` inside a
    string literal is not a comment.
    """
    out = list(text)
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == '"' or c == "'":
            q = c
            i += 1
            while i < n:
                if text[i] == "\\":
                    i += 2
                    continue
                if text[i] == q:
                    i += 1
                    break
                i += 1
            continue
        if c == "/" and i + 1 < n and text[i + 1] == "/":
            while i < n and text[i] != "\n":
                out[i] = " "
                i += 1
            continue
        if c == "/" and i + 1 < n and text[i + 1] == "*":
            while i < n and not (text[i] == "*" and i + 1 < n and text[i + 1] == "/"):
                if text[i] != "\n":
                    out[i] = " "
                i += 1
            if i < n:
                out[i] = " "
                out[i + 1] = " "
                i += 2
            continue
        i += 1
    return "".join(out)


def enclosing_blocks(src: str, pos: int):
    """Every `{ … }` block containing `pos`, innermost LAST.

    Returns a list of (open_offset, close_offset). Straight brace counting over
    comment-stripped, literal-aware text.
    """
    stack, blocks = [], []
    for i, ch in enumerate(src):
        if ch == "{":
            stack.append(i)
        elif ch == "}":
            if not stack:
                continue
            o = stack.pop()
            if o < pos < i:
                blocks.append((o, i))
    blocks.sort(key=lambda b: b[0])
    return blocks


def _split_top(cond: str, op: str):
    """Split `cond` on `op` at paren depth 0."""
    parts, depth, cur, i = [], 0, "", 0
    while i < len(cond):
        c = cond[i]
        if c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
        if depth == 0 and cond.startswith(op, i):
            parts.append(cur)
            cur = ""
            i += len(op)
            continue
        cur += c
        i += 1
    parts.append(cur)
    return parts


def _cond_before(src: str, open_off: int):
    """If the block opening at `open_off` is an `if (…)` body, return the condition."""
    j = open_off - 1
    while j >= 0 and src[j].isspace():
        j -= 1
    if j < 0 or src[j] != ")":
        return None
    depth, k = 0, j
    while k >= 0:
        if src[k] == ")":
            depth += 1
        elif src[k] == "(":
            depth -= 1
            if depth == 0:
                break
        k -= 1
    if k < 0:
        return None
    cond = src[k + 1: j]
    head = src[:k].rstrip()
    if not re.search(r"(^|[^A-Za-z0-9_])if$", head):
        return None
    return cond


def _cond_is_freeze(cond: str, fnbody: str) -> bool:
    """True iff `cond` implies `type_is_freeze(…)` held.

    CONJUNCTION ONLY STRENGTHENS, so one `type_is_freeze` conjunct suffices.
    A `||` anywhere WEAKENS and is refused outright. A negated term is refused.
    A bare identifier is resolved through a SINGLE assignment in the same
    function body — that is the hoisted-bool form, and it is the refactor the
    old reader called a violation.
    """
    if "||" in _strip_parens_only(cond):
        return False
    for term in _split_top(cond, "&&"):
        t = term.strip()
        while t.startswith("(") and t.endswith(")"):
            t = t[1:-1].strip()
        if t.startswith("!"):
            continue
        if "type_is_freeze(" in t:
            return True
        if re.fullmatch(r"[A-Za-z_]\w*", t):
            assigns = re.findall(
                r"(?:^|[;{}\s])(?:const\s+)?(?:bool\s+)?" + re.escape(t) + r"\s*=\s*([^;]+);",
                fnbody,
            )
            if len(assigns) == 1 and "type_is_freeze(" in assigns[0]:
                return True
    return False


def _strip_parens_only(cond: str) -> str:
    """`cond` with parenthesised sub-expressions removed, for a top-level || scan."""
    out, depth = "", 0
    for c in cond:
        if c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
        elif depth == 0:
            out += c
    return out


def guard_state(path: str) -> str:
    """OK | NO-SITE | MULTI-SITE | UNGUARDED."""
    raw = open(path, encoding="utf-8", errors="replace").read()
    src = strip_cpp_comments(raw)
    hits = [m.start() for m in re.finditer(r'"llvm\.readonly"', src)]
    if not hits:
        return "NO-SITE"
    if len(hits) > 1:
        return "MULTI-SITE"
    pos = hits[0]
    blocks = enclosing_blocks(src, pos)
    if not blocks:
        return "UNGUARDED"
    fnbody = src[blocks[0][0]: blocks[0][1]]
    for open_off, _close in blocks:
        cond = _cond_before(src, open_off)
        if cond is not None and _cond_is_freeze(cond, fnbody):
            return "OK"
    return "UNGUARDED"


# ── CLAIMS ───────────────────────────────────────────────────────────────────

CLAIM_OPEN = re.compile(r"^\s*//\s*@claim\s+(\S+)\s*$")
CLAIM_CLOSE = re.compile(r"^\s*//\s*@endclaim\s*$")
CLAIM_BODY = re.compile(r"^\s*//(.*)$")


def claims(path: str):
    """Yield (claim_id, sha256-16) for every delimited claim block, sorted.

    The hash is over the block's NORMALISED text: comment markers stripped,
    all runs of whitespace collapsed to one space. So re-wrapping a claim to a
    different column is free; changing a word is not.
    """
    lines = open(path, encoding="utf-8", errors="replace").read().splitlines()
    found, i, seen = [], 0, set()
    while i < len(lines):
        m = CLAIM_OPEN.match(lines[i])
        if not m:
            if CLAIM_CLOSE.match(lines[i]):
                raise ValueError(
                    "@endclaim at line %d with no open @claim" % (i + 1))
            i += 1
            continue
        cid = m.group(1)
        if cid in seen:
            raise ValueError("duplicate claim id %r at line %d" % (cid, i + 1))
        seen.add(cid)
        body, j, closed = [], i + 1, False
        while j < len(lines):
            if CLAIM_CLOSE.match(lines[j]):
                closed = True
                break
            if CLAIM_OPEN.match(lines[j]):
                raise ValueError(
                    "claim %r (line %d) is not closed before the next @claim"
                    % (cid, i + 1))
            b = CLAIM_BODY.match(lines[j])
            if b is None:
                raise ValueError(
                    "claim %r (line %d) contains a NON-COMMENT line at %d — a "
                    "claim block must be a contiguous run of // comments"
                    % (cid, i + 1, j + 1))
            body.append(b.group(1))
            j += 1
        if not closed:
            raise ValueError("claim %r opened at line %d is never closed"
                             % (cid, i + 1))
        if not any(t.strip() for t in body):
            raise ValueError("claim %r (line %d) has an EMPTY body — an empty "
                             "claim hashes to a constant and asserts nothing"
                             % (cid, i + 1))
        norm = " ".join(" ".join(body).split())
        found.append((cid, hashlib.sha256(norm.encode()).hexdigest()[:16]))
        i = j + 1
    found.sort()
    return found


# ── LOGOS source hygiene ─────────────────────────────────────────────────────


def strip_logos_comments(text: str) -> str:
    """Blank out `//` comments, preserving offsets. `/* */` is NOT stripped —
    over-count is the safe direction for a census."""
    out = list(text)
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == '"':
            i += 1
            while i < n:
                if text[i] == "\\":
                    i += 2
                    continue
                if text[i] == '"':
                    i += 1
                    break
                i += 1
            continue
        if c == "/" and i + 1 < n and text[i + 1] == "/":
            while i < n and text[i] != "\n":
                out[i] = " "
                i += 1
            continue
        i += 1
    return "".join(out)


DECL_RE = re.compile(
    r"^[ \t]*(?:pub[ \t]+)?(struct|enum)[ \t]+([A-Za-z_]\w*)", re.M)


def declarations(text: str):
    """(kind, name, body) for every struct/enum declared at line start.

    `body` is the brace-matched declaration body. A declaration with no body
    (`struct Foo;`) yields an empty body rather than swallowing the rest of the
    file — over-count is safe, but running off the end is not a measurement.
    """
    src = strip_logos_comments(text)
    for m in DECL_RE.finditer(src):
        kind, name = m.group(1), m.group(2)
        k = m.end()
        while k < len(src) and src[k] not in "{;":
            k += 1
        if k >= len(src) or src[k] == ";":
            yield kind, name, ""
            continue
        depth, j = 0, k
        while j < len(src):
            if src[j] == "{":
                depth += 1
            elif src[j] == "}":
                depth -= 1
                if depth == 0:
                    break
            j += 1
        yield kind, name, src[k + 1: j]


FIELD_RE = re.compile(r"(?:^|[,{(])\s*(?:pub\s+)?[A-Za-z_]\w*\s*:\s*([^,}]+)")


def field_types(body: str):
    """The type text of every `name: Type` field in a declaration body."""
    return [t.strip() for t in FIELD_RE.findall(body)]


ROOT_NAME = "UnsafeCell"


def _logos_files(root: str):
    for dirpath, _dirs, files in os.walk(root):
        for f in sorted(files):
            if f.endswith(".logos"):
                yield os.path.join(dirpath, f)


def _scan(stdlib_root: str, repo_root: str):
    """path-relative -> [(kind, name, body)] for every stdlib declaration."""
    per_file = []
    for path in sorted(_logos_files(stdlib_root)):
        text = open(path, encoding="utf-8", errors="replace").read()
        rel = os.path.relpath(path, repo_root)
        per_file.append((rel, list(declarations(text))))
    return per_file


def interior_roots(stdlib_root: str, repo_root: str):
    """`<rel>\t<Name>` for every declaration with a DIRECT UnsafeCell field.

    This is the population `type_is_freeze` detects BY CONSTRUCTION: it answers
    non-Freeze only by finding an UnsafeCell (or the lang item itself) in the
    inline bytes, so every other non-Freeze type in the tree is non-Freeze
    because it reaches one of these.
    """
    out = []
    for rel, decls in _scan(stdlib_root, repo_root):
        for kind, name, body in decls:
            if name == ROOT_NAME and rel.endswith("lang/cell/cell.logos"):
                out.append((rel, name))   # the lang item itself
                continue
            if any(ROOT_NAME in t for t in field_types(body)):
                out.append((rel, name))
    return sorted(set(out))


INDIRECT_RE = re.compile(r"^\s*(\*|&|fn\s*\()")


def transitive(stdlib_root: str, repo_root: str):
    """`<rel>\t<Name>` for declarations that reach a root only THROUGH fields.

    THE INDIRECTION STOP IS THE POINT. `type_is_freeze` stops at `*`/`&`/`fn`,
    which is why `Rc<T> { inner: *mut RcInner<T> }` is Freeze even though
    RcInner holds a cell. A closure without the stop would report Rc, Arc and
    every handle type in the tree as interior-mutable — the opposite of the
    truth, and it would drown the row that matters.
    """
    per_file = _scan(stdlib_root, repo_root)
    roots = set(n for _r, n in interior_roots(stdlib_root, repo_root))
    owner, bodies = {}, {}
    for rel, decls in per_file:
        for _kind, name, body in decls:
            owner.setdefault(name, rel)
            bodies.setdefault(name, []).append(body)
    infected = set(roots)
    changed = True
    while changed:
        changed = False
        for name, blist in bodies.items():
            if name in infected:
                continue
            for body in blist:
                for t in field_types(body):
                    if INDIRECT_RE.match(t):
                        continue
                    for ref in re.findall(r"[A-Za-z_]\w*", t):
                        if ref in infected:
                            infected.add(name)
                            changed = True
                            break
                    if name in infected:
                        break
                if name in infected:
                    break
    return sorted((owner[n], n) for n in infected - roots)


# ── CLI ──────────────────────────────────────────────────────────────────────


def main(argv):
    if len(argv) < 2:
        print("usage: shared_ref_ub_readers.py <guard|claims|interior|transitive> …",
              file=sys.stderr)
        return 2
    cmd = argv[1]
    try:
        if cmd == "guard":
            print(guard_state(argv[2]))
        elif cmd == "claims":
            for cid, h in claims(argv[2]):
                print("%s\t%s" % (cid, h))
        elif cmd == "interior":
            for rel, name in interior_roots(argv[2], argv[3]):
                print("%s\t%s" % (rel, name))
        elif cmd == "transitive":
            for rel, name in transitive(argv[2], argv[3]):
                print("%s\t%s" % (rel, name))
        else:
            print("unknown reader %r" % cmd, file=sys.stderr)
            return 2
    except ValueError as e:
        print("READER-ERROR: %s" % e, file=sys.stderr)
        return 3
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
