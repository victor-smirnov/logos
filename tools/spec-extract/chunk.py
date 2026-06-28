#!/usr/bin/env python3
"""
Deterministic chunker / hasher / planner for the language-spec extraction harness.

This is the DETERMINISTIC skeleton. It contains no model calls. It:

  1. enumerates the source files for each layer (config.json),
  2. slices each file into bounded work-units at NATURAL boundaries
     (C++ top-level definitions; PEG directive blocks + rule heads),
  3. content-hashes every unit,
  4. writes manifest.json,
  5. computes the STALE set by comparing each unit's live hash to the
     `source_hash` recorded in its already-extracted rule artifact,
  6. supports targeted ("точечный") re-runs: --only <spec-id-glob> reverse-maps
     human-readable spec-component ids back to the source units that define them,
     and --touch <unit-glob> forces units by unit id.

Usage:
  chunk.py manifest                       # (re)generate manifest.json for default layers
  chunk.py manifest --layer sema --layer mono
  chunk.py plan                           # emit JSON {units:[...]} of STALE units (for the workflow `args`)
  chunk.py plan --layer grammar           # plan within one layer
  chunk.py plan --only 'expr.cast.*'      # targeted: re-extract units defining matching spec ids
  chunk.py plan --touch 'sema/sema_expr/*'# targeted: force units by unit-id glob
  chunk.py plan --force                   # ignore hashes: every in-scope unit is stale
  chunk.py ids                            # list every assigned spec-component id -> unit (the address book)
  chunk.py status                         # human summary: units, stale count, coverage

Stdlib only. Run from the repo root.
"""

import argparse
import fnmatch
import glob
import hashlib
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
CONFIG_PATH = os.path.join(HERE, "config.json")


def load_config():
    with open(CONFIG_PATH) as f:
        return json.load(f)


def rel(path):
    return os.path.relpath(path, REPO)


def hash_text(text):
    return hashlib.sha256(text.encode("utf-8", "surrogatepass")).hexdigest()[:16]


# ──────────────────────────────────────────────────────────────────────────
# Boundary detection
# ──────────────────────────────────────────────────────────────────────────

_CPP_NAME_BEFORE_PAREN = re.compile(r"([A-Za-z_]\w*)\s*\(")
_CPP_KEYWORD_DEF = re.compile(r"^(?:struct|class|enum|namespace|template|union)\b\s*([A-Za-z_]\w*)?")
# Lines we never treat as a top-level definition boundary even at column 0:
_CPP_NON_BOUNDARY = re.compile(r"^(?:\}|\)|#|//|/\*|\*|using|typedef|extern\s+\"C\"|public:|private:|protected:|else\b|namespace\s*\{)")


def cpp_boundaries(lines):
    """Return list of (line_index, symbol) for column-0 top-level definitions."""
    out = []
    for i, line in enumerate(lines):
        if not line or line[0].isspace():
            continue
        if _CPP_NON_BOUNDARY.match(line):
            continue
        if not re.match(r"^[A-Za-z_~]", line):
            continue
        # A definition either contains a call-shaped '(' (function/method) or
        # is a struct/class/enum/namespace/template/union.
        m = _CPP_NAME_BEFORE_PAREN.search(line)
        if m and "(" in line:
            out.append((i, m.group(1)))
            continue
        mk = _CPP_KEYWORD_DEF.match(line)
        if mk:
            out.append((i, mk.group(1) or line.split()[0]))
    return out


_PEG_DIRECTIVE = re.compile(r"^%([A-Za-z_]\w*)\b")
_PEG_RULE_HEAD = re.compile(r"^\s+([A-Za-z_]\w*)\s*(?::group\s+\w+:\s*)?<-")


def peg_units(lines):
    """
    Yield (start_idx, end_idx_exclusive, symbol) for a PEG file.

    Top-level `%directive { ... }` blocks become one unit each (meta, fields,
    nodes, tokens carry lexical/AST spec). The `%rules { ... }` block is split
    by rule heads, greedily grouped to the target line budget. Single-line
    directives (e.g. `%export { module }`) fold into the next block.
    """
    # Find directive block extents at column 0.
    blocks = []  # (name, start, end_exclusive)
    i = 0
    n = len(lines)
    while i < n:
        m = _PEG_DIRECTIVE.match(lines[i])
        if not m:
            i += 1
            continue
        name = m.group(1)
        start = i
        # Single-line block?  `%export { module }`
        if lines[i].count("{") and lines[i].count("{") == lines[i].count("}"):
            blocks.append((name, start, i + 1))
            i += 1
            continue
        # Multi-line: scan to the matching column-0 `}`.
        depth = lines[i].count("{") - lines[i].count("}")
        j = i + 1
        while j < n and depth > 0:
            depth += lines[j].count("{") - lines[j].count("}")
            j += 1
        blocks.append((name, start, j))
        i = j
    return blocks


# ──────────────────────────────────────────────────────────────────────────
# Unit construction
# ──────────────────────────────────────────────────────────────────────────

def _dedup(symbols):
    seen = {}
    out = []
    for s in symbols:
        s = re.sub(r"[^A-Za-z0-9_.-]", "_", s) or "unit"
        if s in seen:
            seen[s] += 1
            out.append(f"{s}-{seen[s]}")
        else:
            seen[s] = 0
            out.append(s)
    return out


def hard_split(start, end, lines, target, max_lines, window=40):
    """
    Split an oversized span (> max_lines, no internal boundary — e.g. a giant
    `switch`) into ~target-sized parts. Each cut snaps to the nearest blank line
    to the ideal position (blank-line positions are deterministic), so parts
    never split mid-statement. Returns [(start, end)] unchanged if within budget.
    """
    span = end - start
    if span <= max_lines:
        return [(start, end)]
    nparts = max(2, -(-span // target))  # ceil
    ideal = span / nparts
    cuts = [start]
    for k in range(1, nparts):
        target_line = start + round(k * ideal)
        best = target_line
        for off in range(window):
            for cand in (target_line - off, target_line + off):
                if cuts[-1] < cand < end and lines[cand].strip() == "":
                    best = cand
                    break
            else:
                continue
            break
        if best > cuts[-1]:
            cuts.append(best)
    cuts.append(end)
    return list(zip(cuts, cuts[1:]))


def explode_oversized(spans, lines, target, max_lines):
    """Apply hard_split to each span, suffixing .partN when a span is divided."""
    out = []
    for start, end, sym in spans:
        parts = hard_split(start, end, lines, target, max_lines)
        if len(parts) == 1:
            out.append((start, end, sym))
        else:
            for n, (s, e) in enumerate(parts, 1):
                out.append((s, e, f"{sym}.part{n}"))
    return out


def group_boundaries(boundaries, total_lines, target, max_lines):
    """
    Greedily group ordered boundary indices into spans of >= target lines,
    cutting only AT a boundary. A single span never silently exceeds max_lines
    without being its own unit. Returns list of (start, end_exclusive, symbol).
    """
    if not boundaries:
        return [(0, total_lines, "file")]
    spans = []
    idxs = [b[0] for b in boundaries] + [total_lines]
    syms = [b[1] for b in boundaries]
    k = 0
    while k < len(boundaries):
        start = idxs[k]
        sym = syms[k]
        end_k = k + 1
        while end_k < len(boundaries) and (idxs[end_k] - start) < target:
            end_k += 1
        end = idxs[end_k]
        spans.append((start, end, sym))
        k = end_k
    return spans


def build_units(cfg, layers):
    """Return ordered list of unit dicts for the requested layers."""
    target = cfg["unit_target_lines"]
    max_lines = cfg["unit_max_lines"]
    rules_dir = cfg["rules_dir"]
    units = []

    for layer in layers:
        lc = cfg["layers"][layer]
        chunker = lc["chunker"]
        files = []
        for pat in lc["sources"]:
            files.extend(sorted(glob.glob(os.path.join(REPO, pat))))
        for path in files:
            with open(path, encoding="utf-8", errors="surrogatepass") as f:
                text = f.read()
            lines = text.splitlines(keepends=True)
            base = os.path.basename(path)
            stem, ext = os.path.splitext(base)
            # Disambiguate headers from a same-named .cpp (module_loader.cpp vs
            # module_loader.hpp) so their unit-ids — and thus rules_out paths —
            # don't collide. Primary extensions (.cpp, .peg) keep the bare stem
            # so existing unit-ids are unchanged; only headers get a tag.
            if ext not in (".cpp", ".peg"):
                stem = f"{stem}_{ext.lstrip('.')}"
            relpath = rel(path)

            if chunker == "cpp":
                bounds = cpp_boundaries([l.rstrip("\n") for l in lines])
                spans = group_boundaries(bounds, len(lines), target, max_lines)
            elif chunker == "peg":
                spans = []
                for name, start, end in peg_units([l.rstrip("\n") for l in lines]):
                    if name == "rules":
                        # split the rules block by rule heads
                        sub = [l.rstrip("\n") for l in lines[start:end]]
                        heads = [(start + idx, m.group(1))
                                 for idx, l in enumerate(sub)
                                 for m in [_PEG_RULE_HEAD.match(l)] if m]
                        if heads:
                            grouped = group_boundaries(heads, end, target, max_lines)
                            # group_boundaries returned spans relative to head idxs but
                            # end-bounded by `end`; first span should start at block start.
                            grouped[0] = (start, grouped[0][1], "rule-" + grouped[0][2])
                            spans.extend((s, e, sy if sy.startswith("rule-") else "rule-" + sy)
                                         for s, e, sy in grouped)
                        else:
                            spans.append((start, end, name))
                    else:
                        spans.append((start, end, name))
                spans.sort()
            else:
                raise SystemExit(f"unknown chunker {chunker!r}")

            spans = explode_oversized(spans, lines, target, max_lines)
            raw_syms = _dedup([sy for _, _, sy in spans])
            for (start, end, _), sym in zip(spans, raw_syms):
                seg = "".join(lines[start:end])
                unit_id = f"{layer}/{stem}/{sym}"
                units.append({
                    "unit": unit_id,
                    "layer": layer,
                    "file": relpath,
                    "start": start + 1,
                    "end": end,
                    "lines": end - start,
                    "symbol": sym,
                    "hash": hash_text(seg),
                    "source": f"{relpath}#L{start + 1}-L{end}",
                    "rules_out": f"{rules_dir}/{unit_id}.json",
                })
    return units


# ──────────────────────────────────────────────────────────────────────────
# Staleness + targeting
# ──────────────────────────────────────────────────────────────────────────

def read_artifact(rules_out):
    p = os.path.join(REPO, rules_out)
    if not os.path.exists(p):
        return None
    try:
        with open(p) as f:
            return json.load(f)
    except (json.JSONDecodeError, OSError):
        return None


def stale_units(cfg, units, force=False, only=None, touch=None):
    """
    Decide which units to (re-)extract.

      only=<glob>   -> EXCLUSIVE targeted set: units whose existing artifact
                       defines a spec id matching the glob (the точечный path).
      force=True    -> all in-scope units.
      otherwise     -> hash-stale units (missing artifact or mismatched hash).

      touch=<glob>  -> ADDITIVE: union the units whose unit-id matches the glob
                       on top of whichever base was chosen above. Lets you force
                       known-wrong units alongside the naturally-stale ones.
    """
    if only:
        base = [u for u in units
                if (a := read_artifact(u["rules_out"])) is not None
                and any(fnmatch.fnmatch(r.get("id", ""), only) for r in a.get("rules", []))]
    elif force:
        base = list(units)
    else:
        base = [u for u in units
                if (a := read_artifact(u["rules_out"])) is None or a.get("source_hash") != u["hash"]]

    if touch:
        globs = touch if isinstance(touch, (list, tuple)) else [touch]
        have = {u["unit"] for u in base}
        base += [u for u in units
                 if u["unit"] not in have and any(fnmatch.fnmatch(u["unit"], g) for g in globs)]
    return base


def attach_prior_ids(units):
    """For each unit, attach the spec ids + domains already assigned: ids for
    stability (agent must reuse them), domains so the workflow always rebuilds a
    previously-touched section even if the re-extraction agent under-reports it."""
    for u in units:
        art = read_artifact(u["rules_out"])
        rules = art.get("rules", []) if art else []
        u["prior_ids"] = [r.get("id") for r in rules]
        u["prior_domains"] = sorted({r.get("domain") for r in rules if r.get("domain")})
    return units


# ──────────────────────────────────────────────────────────────────────────
# Collisions (one id must address exactly one rule — required for spec tests)
# ──────────────────────────────────────────────────────────────────────────

import difflib  # noqa: E402


def iter_all_rules(cfg):
    """Yield (rel_file, rule_index, rule_dict) over every artifact on disk."""
    root = os.path.join(REPO, cfg["rules_dir"])
    for path in sorted(glob.glob(os.path.join(root, "**", "*.json"), recursive=True)):
        try:
            art = json.load(open(path))
        except (json.JSONDecodeError, OSError):
            continue
        for i, r in enumerate(art.get("rules", [])):
            yield rel(path), i, r


def find_collisions(cfg):
    """Group rules by id; a collision = one id carried by >1 rule. Each variant
    keeps its file + full rule + a similarity score vs the first (low score =
    genuinely different rules = fragmentation; high = benign corroboration)."""
    by = {}
    for f, _, r in iter_all_rules(cfg):
        by.setdefault(r.get("id", ""), []).append({"file": f, "rule": r})
    out = []
    for rid, variants in sorted(by.items()):
        if len(variants) < 2:
            continue
        s0 = variants[0]["rule"].get("statement", "")
        sim = max((difflib.SequenceMatcher(None, s0, v["rule"].get("statement", "")).ratio()
                   for v in variants[1:]), default=1.0)
        out.append({"id": rid, "count": len(variants), "similarity": round(sim, 2),
                    "variants": variants})
    return out


def cmd_collisions(cfg, args):
    cols = find_collisions(cfg)
    if args.json:
        json.dump({"collisions": cols}, sys.stdout, indent=2)
        print()
    else:
        frag = [c for c in cols if c["similarity"] <= 0.85]
        for c in cols:
            kind = "FRAG" if c["similarity"] <= 0.85 else "corrob"
            print(f"{kind:6s} {c['id']:50s} x{c['count']} sim={c['similarity']}")
            for v in c["variants"]:
                print(f"         - [{v['file'].split('rules/')[-1]}] {v['rule'].get('statement','')[:70]}")
    print(f"{len(cols)} colliding ids ({sum(1 for c in cols if c['similarity'] <= 0.85)} fragmentation)",
          file=sys.stderr)


def cmd_apply_dedup(cfg, args):
    """Apply model dedup decisions: {decisions:[{id, final_rules:[{file, rule}]}]}.
    For each decision: drop every rule carrying the collision id from every file
    that held it, then insert each final rule into its target file. Deterministic."""
    decisions = json.load(open(args.decisions))["decisions"]
    id_pat = re.compile(r"^[a-z][a-z0-9]*\.[a-z0-9][a-z0-9-]*\.[a-z0-9][a-z0-9-]*$")
    # Original-state map: which files hold each colliding id (precomputed once).
    holders_map = {}
    for f, _, r in iter_all_rules(cfg):
        holders_map.setdefault(r.get("id", ""), set()).add(f)
    colliding = {d["id"] for d in decisions}
    # An id is "existing elsewhere" if it is NOT one of the collisions being resolved.
    existing_ids = {rid for rid in holders_map if rid not in colliding}

    changed = {}  # rel_file -> artifact dict (loaded once, mutated)

    def load(relf):
        if relf not in changed:
            changed[relf] = json.load(open(os.path.join(REPO, relf)))
        return changed[relf]

    applied = merged = split = 0
    new_ids = set()
    for d in decisions:
        cid = d["id"]
        for relf in sorted(holders_map.get(cid, [])):
            art = load(relf)
            art["rules"] = [r for r in art["rules"] if r.get("id") != cid]
        for fr in d["final_rules"]:
            relf, rule = fr["file"], fr["rule"]
            rid = rule.get("id", "")
            if not id_pat.match(rid):
                raise SystemExit(f"apply-dedup: bad id {rid!r} for collision {cid}")
            if rid in existing_ids or rid in new_ids:
                raise SystemExit(f"apply-dedup: target id {rid!r} collides with an existing/other id")
            new_ids.add(rid)
            load(relf)["rules"].append(rule)
        merged += len(d["final_rules"]) == 1
        split += len(d["final_rules"]) > 1
        applied += 1

    for relf, art in changed.items():
        with open(os.path.join(REPO, relf), "w") as f:
            json.dump(art, f, indent=2, ensure_ascii=False)
            f.write("\n")
    remaining = len(find_collisions(cfg))
    print(f"applied {applied} decisions ({merged} merged, {split} split) across "
          f"{len(changed)} files; collisions remaining: {remaining}", file=sys.stderr)


# ──────────────────────────────────────────────────────────────────────────
# Commands
# ──────────────────────────────────────────────────────────────────────────

def resolve_layers(cfg, requested):
    if requested:
        for l in requested:
            if l not in cfg["layers"]:
                raise SystemExit(f"unknown layer {l!r}; known: {list(cfg['layers'])}")
        return requested
    return cfg["default_layers"]


def cmd_manifest(cfg, args):
    layers = resolve_layers(cfg, args.layer)
    units = build_units(cfg, layers)
    manifest = {
        "repo": REPO,
        "layers": layers,
        "config_hash": hash_text(json.dumps(cfg, sort_keys=True)),
        "unit_count": len(units),
        "units": units,
    }
    out = os.path.join(REPO, cfg["manifest"])
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "w") as f:
        json.dump(manifest, f, indent=2)
    print(f"wrote {rel(out)}: {len(units)} units across {layers}", file=sys.stderr)


def cmd_plan(cfg, args):
    layers = resolve_layers(cfg, args.layer)
    units = build_units(cfg, layers)
    stale = stale_units(cfg, units, force=args.force, only=args.only, touch=args.touch)
    attach_prior_ids(stale)
    plan = {
        "layers": layers,
        "rules_dir": cfg["rules_dir"],
        "spec_dir": cfg["spec_dir"],
        "sections": cfg["sections"],
        "id_domains": cfg["id_domains"],
        "schema_path": "tools/spec-extract/rule.schema.json",
        "total_units": len(units),
        "stale_count": len(stale),
        "units": stale,
    }
    json.dump(plan, sys.stdout, indent=2)
    print(file=sys.stdout)
    print(f"plan: {len(stale)}/{len(units)} units stale in {layers}", file=sys.stderr)


def cmd_ids(cfg, args):
    layers = resolve_layers(cfg, args.layer)
    units = build_units(cfg, layers)
    rows = []
    for u in units:
        art = read_artifact(u["rules_out"])
        if not art:
            continue
        for r in art.get("rules", []):
            rows.append((r.get("id", "?"), u["unit"], r.get("title", "")))
    rows.sort()
    for rid, unit, title in rows:
        print(f"{rid}\t{unit}\t{title}")
    print(f"{len(rows)} spec-component ids", file=sys.stderr)


def cmd_status(cfg, args):
    layers = resolve_layers(cfg, args.layer)
    units = build_units(cfg, layers)
    stale = stale_units(cfg, units)
    extracted = sum(1 for u in units if read_artifact(u["rules_out"]))
    total_rules = 0
    for u in units:
        art = read_artifact(u["rules_out"])
        if art:
            total_rules += len(art.get("rules", []))
    by_layer = {}
    for u in units:
        by_layer.setdefault(u["layer"], [0, 0])
        by_layer[u["layer"]][0] += 1
    for u in stale:
        by_layer[u["layer"]][1] += 1
    print(f"layers:        {layers}")
    print(f"units:         {len(units)}")
    print(f"extracted:     {extracted}")
    print(f"stale:         {len(stale)}")
    print(f"spec rules:    {total_rules}")
    print("per layer (units / stale):")
    for l, (tot, st) in sorted(by_layer.items()):
        print(f"  {l:10s} {tot:4d} / {st}")


def main():
    cfg = load_config()
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)

    for name in ("manifest", "plan", "ids", "status"):
        sp = sub.add_parser(name)
        sp.add_argument("--layer", action="append", help="restrict to layer(s); repeatable")
        if name == "plan":
            sp.add_argument("--force", action="store_true", help="re-extract every in-scope unit")
            sp.add_argument("--only", help="targeted: glob over spec-component ids (reverse-mapped to units)")
            sp.add_argument("--touch", action="append", help="force units by unit-id glob (additive; repeatable)")

    spc = sub.add_parser("collisions", help="report ids carried by >1 rule (must be 0 for spec tests)")
    spc.add_argument("--json", action="store_true", help="emit full variant context as JSON (feed the dedup workflow)")
    spa = sub.add_parser("apply-dedup", help="apply model dedup decisions to the corpus")
    spa.add_argument("decisions", help="path to the decisions JSON {decisions:[{id, final_rules:[{file, rule}]}]}")

    args = p.parse_args()
    {"manifest": cmd_manifest, "plan": cmd_plan, "ids": cmd_ids, "status": cmd_status,
     "collisions": cmd_collisions, "apply-dedup": cmd_apply_dedup}[args.cmd](cfg, args)


if __name__ == "__main__":
    main()
