#!/usr/bin/env python3
"""store_api_census.py REPO LEDGER [--write]   (ledger: tests/logos/store_api.ledger)

ADR 0039 M1 — every use of the two persistent stacks' surface, mapped to the
D4 vocabulary / D7 placement trait, pinned. A row moves only with the ledger.

Population: stdlib/, tests/, conuco/ (*.logos) and src/compiler (*.cpp, *.hpp),
`//` comments stripped. A use is a whole-word spelling of a SYMBOL below.

Attribution is by the file, not the spelling: a symbol of stack X counts only
in a file inside X's package or importing it (the D4 vocabulary reuses names
such as `Snap`). Spellings in other files are listed as `# not counted` rows.
`create_ctr` / `open_ctr` were defined by BOTH stacks until deem.data was
deleted (ADR 0039 M4b); a file importing neither is counted under stack `?`
(a pinned row, so a new ambiguous use is visible);
the compiler's own spellings (sema diagnostics, intrinsics) under `compiler`.

Refusals (exit 2, not a pin diff):
  * a SYMBOL's definition count in the stacks' packages (mem/bt, lcm/canon,
    and, until ADR 0039 M4b, lcm/deem/data) is not the one listed — a rename or a new
    overload would otherwise zero or merge its rows silently;
  * a `pub fn store_*` in memstore.logos that no SYMBOL claims.

Not measured: the `Snapshot` trait's methods (`commit`, `remove`, `status`,
`set_as_branch`, ...) — the spelling is shared with unrelated types. They are
priced by the compiler when M2 splits `Snapshot` into Snapshot/Workspace.
"""
import os, re, sys
from collections import defaultdict

MB = 'mem.bt'   # deem.data (the second stack) was deleted at ADR 0039 M4b

# (symbol, stack, D4/D7 target, definition regex, expected definition count)
SYMBOLS = [
    ('MemoryStore',            MB, 'VersionStore (in-memory model, D10)', r'pub struct MemoryStore\b', 1),
    ('MemorySnapshot',         MB, 'Snapshot + Workspace instance; NodeStore (D7)', r'pub struct MemorySnapshot\b', 1),
    ('dyn Snapshot',           MB, 'Snapshot (read) / Workspace (write) — split at M2', r'pub trait Snapshot\b', 1),
    ('MSDOWNCAST',             MB, 'NodeStore (D7) replaces the dyn_data downcast', r'pub struct MemorySnapshot\b', 1),
    ('NodeStore',              MB, 'NodeStore (D7): node placement + the directory', r'pub trait NodeStore\b', 1),
    ('node_store_of',          MB, 'NodeStore (D7): the checked step to the privileged tier', None, 1),
    ('StoreProfile',           MB, 'NodeStore (D7): a node profile bound to its model', r'pub trait StoreProfile\b', 1),
    ('store_of',               MB, 'NodeStore (D7): the profile store, checked', None, 1),
    ('memory_snapshot_of',     MB, 'the in-memory model\'s tools and engine tests only', None, 1),
    ('store_new',              MB, 'VersionStore construction (model)', None, 1),
    ('store_fork',             MB, 'VersionStore::fork -> Workspace', None, 1),
    ('store_fork_arc',         MB, 'VersionStore::fork -> Workspace', None, 1),
    ('store_fork_branch',      MB, 'VersionStore::fork(branch head) -> Workspace', None, 1),
    ('store_fork_from',        MB, 'VersionStore::fork(&Snapshot) -> Workspace', None, 1),
    ('store_find',             MB, 'VersionStore: open a committed Snapshot', None, 1),
    ('store_find_branch',      MB, 'VersionStore branches', None, 1),
    ('store_master',           MB, 'VersionStore branches', None, 1),
    ('store_set_branch',       MB, 'VersionStore branches', None, 1),
    ('store_set_master',       MB, 'VersionStore branches', None, 1),
    ('store_remove_branch',    MB, 'VersionStore branches', None, 1),
    ('store_branches',         MB, 'VersionStore branches', None, 1),
    ('store_branch_head',      MB, 'VersionStore heads', None, 1),
    ('store_branch_heads',     MB, 'VersionStore heads', None, 1),
    ('store_heads',            MB, 'VersionStore heads', None, 1),
    ('store_root_id',          MB, 'VersionStore log', None, 1),
    ('store_snapshot_parent',  MB, 'Snapshot::parents', None, 1),
    ('store_children_of',      MB, 'VersionStore log', None, 1),
    ('store_parents',          MB, 'Snapshot::parents (D8: more than one after a merge)', None, 1),
    ('store_merge',            MB, 'VersionStore::merge (D8, select-one)', None, 1),
    ('store_set_label',        MB, 'VersionStore labels (D10, from deem.data)', None, 1),
    ('store_label',            MB, 'VersionStore labels (D10, from deem.data)', None, 1),
    ('store_log',              MB, 'VersionStore log (first parent)', None, 1),
    ('store_roots',            MB, 'VersionStore log', None, 1),
    ('store_linear_history',   MB, 'VersionStore log', None, 1),
    ('store_snapshot_status',  MB, 'VersionStore log', None, 1),
    ('store_active_snapshots', MB, 'VersionStore log', None, 1),
    ('store_drop',             MB, 'VersionStore release', None, 1),
    ('store_pack',             MB, 'VersionStore release', None, 1),
    ('store_save',             MB, 'model durability (D6), not vocabulary', None, 1),
    ('store_load',             MB, 'model durability (D6), not vocabulary', None, 1),
    ('create_ctr',             None, 'Ctr: create in a Workspace', None, 1),
    ('open_ctr',               None, 'Ctr: open in a Snapshot / Workspace', None, 1),
    ('create_ctr_rc',          MB, 'Ctr: create in a Workspace', None, 1),
    ('open_ctr_rc',            MB, 'Ctr: open in a Snapshot / Workspace', None, 1),
]
PKG = {MB: ('stdlib/mem/bt/', 'stdlib/lcm/canon/')}
IMP = {MB: re.compile(r'^\s*use\s+logos\.(mem\.bt|lcm\.canon)\b', re.M)}
DOWNCAST = re.compile(r'dyn_data\([^;]*?\)\s*as\s*\*(const|mut)\s+MemorySnapshot\b')
UNCLAIMED = [('stdlib/mem/bt/memstore.logos', r'^pub fn (store_\w+)')]
# handle.logos / store.logos items that are internal to the package (no D4 role).
INTERNAL = {'Store'}

def strip(text):
    out = []
    for ln in text.split('\n'):
        i = ln.find('//')
        out.append(ln if i < 0 else ln[:i])
    return '\n'.join(out)

def population(repo):
    for top, exts in (('stdlib', ('.logos',)), ('tests', ('.logos',)), ('conuco', ('.logos',)),
                      ('src/compiler', ('.cpp', '.hpp'))):
        for d, dirs, files in os.walk(os.path.join(repo, top)):
            dirs[:] = [x for x in dirs if x != 'probes']
            for f in files:
                if f.endswith(exts):
                    p = os.path.join(d, f)
                    yield os.path.relpath(p, repo), open(p, errors='replace').read()

def area(rel):
    parts = rel.split('/')
    if parts[0] == 'stdlib':
        return '/'.join(parts[:3]) if parts[1] in ('mem', 'lcm', 'lang', 'std') else '/'.join(parts[:2])
    if parts[0] == 'tests':
        return '/'.join(parts[:3]) if len(parts) > 3 else 'tests'
    return parts[0] if parts[0] != 'src' else 'src/compiler'

def stack_of(rel, raw):
    if rel.startswith('src/'):
        return 'compiler'
    own = [s for s, pre in PKG.items() if rel.startswith(pre)]
    if own:
        return own[0]
    imp = [s for s, rx in IMP.items() if rx.search(raw)]
    return imp[0] if len(imp) == 1 else '?'

def main():
    repo, ledger = sys.argv[1], sys.argv[2]
    write = '--write' in sys.argv[3:]
    files = list(population(repo))
    refuse = []
    stdlib_text = {r: t for r, t in files if r.startswith('stdlib/')}
    # Definitions are looked up in the two stacks' own packages only; the D4
    # vocabulary (logos.mem.version) is the migration TARGET and reuses names.
    stack_text = {r: t for r, t in stdlib_text.items() if r.startswith(PKG[MB])}
    for sym, st, _, drx, want in SYMBOLS:
        rx = re.compile(drx if drx else r'^pub fn ' + re.escape(sym) + r'\b', re.M)
        n = sum(len(rx.findall(t)) for t in stack_text.values())
        if n != want:
            refuse.append(f'{sym}: {n} definition(s) in the stacks\' packages, ledger expects {want}')
    claimed = {s for s, *_ in SYMBOLS} | INTERNAL
    for rel, rx in UNCLAIMED:
        for name in re.findall(rx, stdlib_text.get(rel, ''), re.M):
            if name not in claimed:
                refuse.append(f'{rel}: `{name}` is public and no SYMBOL claims it')
    if refuse:
        print('REFUSE (the census cannot be trusted until the SYMBOLS table is updated):')
        for r in refuse:
            print('  ' + r)
        sys.exit(2)

    uses = defaultdict(int); nfiles = defaultdict(set); stray = defaultdict(int)
    for rel, raw in files:
        code = strip(raw)
        ar = area(rel)
        fst = stack_of(rel, raw)
        for sym, st, _, _, _ in SYMBOLS:
            if sym == 'MSDOWNCAST':
                n = len(DOWNCAST.findall(code))
            elif ' ' in sym:
                n = len(re.findall(re.escape(sym).replace(r'\ ', r'\s+') + r'\b', code))
            else:
                n = len(re.findall(r'(?<![\w.])' + re.escape(sym) + r'\b', code))
            if n and st and fst not in (st, 'compiler'):
                stray[(sym, fst, ar)] += n
                continue
            if n:
                key = (sym, st if st else fst, ar)
                uses[key] += n; nfiles[key].add(rel)
    target = {s: t for s, _, t, _, _ in SYMBOLS}
    order = {s: i for i, (s, *_) in enumerate(SYMBOLS)}
    rows = ['# ADR 0039 M1 — store API census. Regenerate: tests/logos/store_api_census.py . %s --write' % os.path.relpath(ledger, repo),
            '# symbol\tstack\tarea\tuses\tfiles\ttarget']
    for k in sorted(uses, key=lambda k: (order[k[0]], k[1], k[2])):
        rows.append('\t'.join([k[0], k[1], k[2], str(uses[k]), str(len(nfiles[k])), target[k[0]]]))
    tot = defaultdict(int)
    for (s, st, a), n in uses.items():
        tot[st] += n
    for k in sorted(stray):
        rows.append('# not counted (file is outside the stack)\t' + '\t'.join([k[0], k[1], k[2], str(stray[k])]))
    rows.append('# totals\t' + '\t'.join(f'{s}={tot[s]}' for s in sorted(tot)))
    text = '\n'.join(rows) + '\n'
    if write:
        open(ledger, 'w').write(text)
        print(f'wrote {ledger}: {len(rows) - 3} rows; ' + ', '.join(f'{s}={tot[s]}' for s in sorted(tot)))
        return
    old = open(ledger).read() if os.path.exists(ledger) else ''
    if old == text:
        print(f'store API census: {len(rows) - 3} rows match the ledger; ' + ', '.join(f'{s}={tot[s]}' for s in sorted(tot)))
        return
    import difflib
    print('FAIL: the store API census moved. Re-derive with --write in the same commit and say why:')
    sys.stdout.writelines(difflib.unified_diff(old.splitlines(True), text.splitlines(True), 'ledger', 'measured'))
    sys.exit(1)

main()
