#!/usr/bin/env bash
# emit_shards_gate.sh LOGOSC STDLIB_BIN_DIR REPO_ROOT WORK_DIR
#
# SHARDED EMISSION, ASSERTED — because nothing in the corpus set the knob.
#
# MEASURED 2026-08-04, repo-wide: three occurrences of `LOGOS_EMIT_SHARDS`, all
# three inside src/compiler/emit_module.cpp (two comments and the getenv). No
# test, no CMake target, no script set it. Every shard sweep in that arc was run
# BY HAND, so a regression in sharded emission was invisible to every tier —
# the fifth recorded way a gate lies (a test that is not in the suite reads as a
# pass, and a code path no test enters reads the same way). The source comment
# at the shards default says so itself: "Pin it before anyone leans on this
# path." This is that pin.
#
# ── WHY A GATE AT ALL FOR A KNOB THAT IS OFF ────────────────────────────────
# Sharding buys BUILD time and sells RUN time roughly one for one (measured
# 2026-08-04: the dynamic Deem aggregate loop goes 1.52 s unsharded -> 2.74 s at
# 4 shards, while the `mem` layer goes 115.6 s -> 49.9 s at 32). It is an opt-in
# for iteration and it stays off by default. But "off by default" is exactly the
# condition under which a path rots unobserved, and this one is not inert: it
# changes the LINKAGE of compiler-synthesised drop glue and it partitions every
# function body in the program. It was also the SENSOR that surfaced two real
# miscompiles (`6346fb4d`, `8372381b`). A sensor nobody can trust is not a
# sensor.
#
# ── THE ORACLE IS THE ARCHIVE, NOT THE PROGRAM ─────────────────────────────
# THIS IS THE LOAD-BEARING DESIGN DECISION AND IT WAS MEASURED, not assumed.
# CONTROL 1b: the shard predicate in mlir_gen.cpp was forced to skip EVERY body.
# All four shard objects were then written, all four were 496 bytes, all four
# were valid ELF, and between them they defined ZERO globals against 6374 at one
# shard. And the consumer STILL compiled, STILL linked, and STILL printed the
# correct SUM=360 — because the archive's `.wr0` member carries the AST and
# logosc recompiles locally whatever it cannot find in the archive (it prints
# "dependency bodies will be RECOMPILED from their archived ASTs instead of
# linked" while doing it). Half a module can vanish and an end-to-end test sees
# nothing. That is the same sentence as the 2026-08-03 event this whole guard
# exists for. So every detecting assertion below reads `nm` over the ARCHIVE
# MEMBERS; the program is kept only for the LINK, where duplicate definitions
# land.
#
# Equally: the gate never re-derives the FNV partition. A checker that
# recomputes the answer the same way the subject does is not an oracle — it is
# the drift this repo has already paid for twice. Everything here is a relation
# between two EMITTED artifacts (S=1 vs S=4, W=4 vs W=1) that must hold whatever
# the partition function is.
#
# ── WHAT IS ASSERTED, AND WHICH DELIBERATE BREAK EACH ONE CAUGHT ───────────
# Every number below was MEASURED by breaking the compiler on purpose, rebuilding
# logosc, and running THIS test through ctest. Five controls, each restored and
# re-run green afterwards:
#   CONTROL 1   mlir_gen.cpp shard predicate `!=` -> `==`     ctest rc 8 -> 0
#   CONTROL 1b  the same predicate forced to skip everything  ctest rc 8 -> 0
#   CONTROL 2   compile_pipeline.cpp `if (opts.shard_count > 1)`
#               on the drop-glue relinkage disabled           ctest rc 8 -> 0
#   CONTROL 3   a shard object truncated to zero bytes, WITH emit_module's own
#               size guard bypassed                           ctest rc 8 -> 0
#   CONTROL 3a  the same truncation with the guard LIVE — the compiler refuses,
#               and the gate must propagate that refusal      ctest rc 8 -> 0
#
#  A. MEMBER COUNT is shards+3. Derived from emit_module.cpp (`ar rcs` takes the
#     shard objects, else the single object, plus the .writ0, .pkgi and .imp
#     members), and cross-checked against the recorded 4/5/7/35 for
#     liblogos-mem.a at 1/2/4/32 shards. This is the canary that the env var was
#     READ; it fires if the getenv is deleted or renamed.
#
#  A2. NEGATIVE CONTROL: the S=1 and S=4 archives must NOT be byte-identical,
#     and the S=4 archive must carry members `*.s0.o`..`*.s3.o`. Without this
#     the whole gate could be silently comparing S=1 against S=1 and reporting
#     every equality it checks. An "equal" that cannot be unequal is not a test.
#
#  B. EVERY SHARD MEMBER IS >= 64 BYTES — the same floor emit_module's own guard
#     applies, restated OUTSIDE the compiler so it is still checked if the guard
#     is ever removed or weakened. CONTROL 3 fires here ("shard member
#     shardgate.o.s1.o is 0 bytes"), and CONTROL 3a shows the other half: with
#     the guard live the compiler refuses, emit-module exits non-zero, and the
#     gate reports THAT rather than proceeding to compare half an artifact.
#     ⚠ AND THE 64-BYTE FLOOR IS BELOW THE FAILURE IT TARGETS: measured under
#     CONTROL 1b, a shard object holding NO bodies at all is 496 bytes, eight
#     times the floor. B catches truncation, NOT emptiness. C catches emptiness.
#     Whether the compiler's own guard should become a symbol-count or section
#     check is a design call and nothing here presumes it.
#
#  C. THE DEFINED-GLOBAL NAME SET OF THE FOUR SHARD OBJECTS, UNIONED, EQUALS
#     THE DEFINED-GLOBAL NAME SET OF THE SINGLE OBJECT. Splitting a module is
#     supposed to move bodies between objects and change nothing about which
#     bodies exist. THIS IS THE ASSERTION THAT SEES CONTROL 1b: under it the
#     union went to 0 against 6374, while the member count stayed 7, the size
#     floor stayed green (496-byte objects) and the consumer still printed
#     SUM=360 against four empty shards.
#     It carries a PER-SHARD floor (>= 1 defined global) and a FIXTURE floor
#     (>= 100 at S=1) beside the set equality, because "the two sets are equal"
#     also holds when one shard took everything and three took nothing, and
#     holds twice over when nm returned nothing at all.
#     MEASURED at HEAD: 1555 / 1605 / 1565 / 1661 per shard, union 6374 = 6374,
#     0 either side. CONTROL 3 (a shard truncated to zero bytes with the
#     compiler's own guard bypassed) shows here as only-S1 = 1601.
#
#  C2. NO NAME DEFINED IN MORE THAN ONE SHARD OBJECT CARRIES A STRONG nm LETTER
#     (T/D/R/B). Duplicated-and-strong in one archive is a `multiple definition`
#     waiting for a consumer that pulls both members. At HEAD: exactly 4 names
#     appear in more than one shard, letters WWWW and VVVV, 0 strong.
#     CONTROL 1 (shard predicate `!=` -> `==`, i.e. every shard emits every body
#     it does not own) drove this to 6370 strong duplicates while the member
#     count and the NAME SET were both still perfectly green — C is blind to it,
#     C2 is not. CONTROL 2 drove it to 2.
#
#  D. EVERY `__drop_in_place__*` DEFINED IN A SHARD OBJECT IS `W` (LinkOnceODR),
#     AND THE SAME NAMES ARE `T` AT ONE SHARD. That is exactly what
#     compile_pipeline.cpp does under `if (opts.shard_count > 1)`, and why:
#     the glue is synthesised on demand while lowering, so every shard that
#     drops a T emits its own copy, and internal linkage will not do because a
#     vtable in another shard holds a pointer to it. CONTROL 2 (that `if`
#     disabled) leaves them `T` in all four shards and fires here.
#     WITH A FIXTURE FLOOR: at least 2 distinct such names must have been
#     examined. "0 mismatches" over 0 names is not a pass — that is the
#     `tags=0` lesson from unit_graph_gate.sh, restated.
#
#  F. THE S=4/W=1 ARCHIVE IS BYTE-IDENTICAL TO THE S=4/W=4 ARCHIVE. The source
#     comment claims exactly this ("the partition is a pure function of the link
#     names"), and W=1 is the sequential control for the thread pool. Any
#     worker-count dependence — a race in the pool, an ordering leak, a
#     scheduling-dependent name — shows up here as a byte difference.
#     ⚠ THIS ASSERTION REQUIRES `id shardgate_fixture` IN THE MANIFEST. Without
#     an explicit id, module_manifest.cpp derives the module tag as FNV-1a of
#     the TARGET OUTPUT PATH and sema bakes it into every mangled name as
#     `$M<id>`, so two builds to two paths differ by construction and this would
#     be comparing path hashes. It cost a wrong "nondeterminism" diagnosis once.
#
#  E. THE CONSUMER LINKS AND PRINTS `SUM=360` against both archives. 360 is
#     derived by hand: sum(i, 0..15) + sum(2i, 0..15) = 120 + 240. Kept as
#     end-to-end sanity and as the place a duplicate STRONG symbol becomes a
#     link error — NOT as a detector: see the CONTROL 1b paragraph above, it
#     passed this while the shards were empty.
#
# ── TIER, AND THE COST ACCEPTED ────────────────────────────────────────────
# This runs at L4 / ctest-summary only. It is a plain `add_test` with no sampled
# label, exactly like logos_09_unit_graph and for the same reason: the L1-L3
# samplers in test-levels.sh enumerate `.logos` files under pass/ and fail/ and
# cannot reach a module build. MEASURED cost THROUGH ctest: 20.1 s on a quiet
# box, 37.9 s with three sibling builds running — three --emit-module runs plus
# two consumer compile+link+run. Against a 710 s suite whose critical path is
# the 153 s logos_26_exhaustive_full, the added wall clock under `ctest -j32`
# is ~0.
# It is NOT wired into L2. That would cost ~+40% on a 65 s tier to pin a knob
# that is off by default and, by the 2026-08-04 price measurement, will stay off
# for anything shipped or measured — so a sharding regression cannot reach a
# shipped artifact, and the full suite catches it at most one commit later. That
# is an argued narrowing, not an oversight; if it should be at L2 the wiring is
# the four-line block at test-levels.sh:107-118 and the cost is the +40%.
set -uo pipefail

LOGOSC="${1:?logosc}"
LIBDIR="${2:?stdlib bin dir}"
REPO="${3:?repo root}"
WORK="${4:?work dir}"

rm -rf "$WORK" || exit 1
mkdir -p "$WORK" || exit 1
# ABSOLUTISE EVERYTHING. Half of this gate runs inside `cd "$REPO"` subshells or
# `cd "$X1"` extraction dirs, so a relative argument silently names a different
# file there — measured on the first run: `ar x` reported "No such file or
# directory" for an archive that had just been written and verified.
WORK="$(cd "$WORK" && pwd)"                       || exit 1
REPO="$(cd "$REPO" && pwd)"                       || exit 1
LIBDIR="$(cd "$LIBDIR" && pwd)"                   || exit 1
case "$LOGOSC" in /*) ;; *) LOGOSC="$(cd "$(dirname "$LOGOSC")" && pwd)/$(basename "$LOGOSC")" ;; esac
fail=0
note() { printf '%s\n' "$*"; }
bad()  { printf 'FAIL: %s\n' "$*"; fail=1; }

# ⚠ NEVER `producer | grep -q` UNDER `set -o pipefail`: grep -q exits on the
# first match and closes the pipe, the producer takes SIGPIPE, and the pipeline
# reports FAILURE precisely when the thing was FOUND. Count instead — `grep -c`
# drains its input.
count_lines() { grep -c '' "$1" 2>/dev/null || true; }

# ⚠ NM PARSING. Logos link names contain SPACES — e.g.
# `logos_lang..logos.lang.iter.FilterIter$G2$Bytes$u8__inspect__g__…__fn(T) -> void__T`
# so `awk '{print $NF}'` and `nm -P` both shred them (that mis-parse produced a
# bogus "60 strong duplicates" reading once). The name is everything after the
# address and the one-letter class. `-g` is not optional either: without it 132
# local `.LCPI####_#` constant-pool labels differ between S=1 and S=4 and swamp
# the signal.
# Emits "<letter>\t<name>" per defined global.
nm_pairs() { nm -g --defined-only "$@" 2>/dev/null \
    | sed -nE 's/^[0-9a-fA-F]+ (.) (.*)$/\1\t\2/p'; }

# ── build the three archives ───────────────────────────────────────────────
# TMPDIR is redirected into the work dir ON PURPOSE. emit_module puts its
# intermediates in `temp_directory_path() / ("logos_emit_" + manifest.name)`,
# keyed on the module NAME only — not the output path, not the pid. Two
# concurrent builds of the same module name write the same
# `/tmp/logos_emit_<name>/<name>.o`; that is a live cross-worktree hazard today
# (43 such directories in /tmp during this arc, with five concurrent logosc
# processes sharing one of them). std::filesystem honours TMPDIR, so pointing it
# at $WORK makes this gate immune without touching the compiler.
emit() {  # emit <out.a> <shards> <workers>
    local out="$1" sh="$2" w="$3"
    ( cd "$REPO" && TMPDIR="$WORK/tmp" LOGOS_EMIT_SHARDS="$sh" LOGOS_EMIT_WORKERS="$w" \
        "$LOGOSC" --emit-module tests/logos/shardgate/shardgate.module \
        -L "$LIBDIR" -o "$out" ) > "$out.log" 2>&1
    return $?
}
mkdir -p "$WORK/tmp" || exit 1

A1="$WORK/libshardgate_s1.a"
A4="$WORK/libshardgate_s4.a"
A4W1="$WORK/libshardgate_s4w1.a"

for spec in "$A1 1 1" "$A4 4 4" "$A4W1 4 1"; do
    set -- $spec
    # ⚠ CAPTURE THE STATUS INTO A VARIABLE. `if ! emit …; then bad "(rc=$?)"`
    # prints the status of the NEGATION, not of the emit — measured: it reported
    # `rc=0` for the run whose message on the next line was "refusing to archive
    # a truncated module". A status read one command too late is the same lie as
    # a status read through `tee`.
    emit "$1" "$2" "$3"; emit_rc=$?
    if [ "$emit_rc" != "0" ]; then
        bad "emit-module FAILED at shards=$2 workers=$3 (rc=$emit_rc):"
        tail -5 "$1.log"
        echo "emit_shards_gate: FAILED"
        exit 1
    fi
done
note "built: S=1, S=4/W=4, S=4/W=1"

# ── A. member count == shards + 3 ──────────────────────────────────────────
ar t "$A1"   > "$WORK/t1.txt" 2>/dev/null
ar t "$A4"   > "$WORK/t4.txt" 2>/dev/null
ar t "$A4W1" > "$WORK/t4w1.txt" 2>/dev/null
n1=$(count_lines "$WORK/t1.txt")
n4=$(count_lines "$WORK/t4.txt")
n4w1=$(count_lines "$WORK/t4w1.txt")
[ "$n1" = "4" ] || bad "S=1: archive has $n1 members, expected 4 (one object + .writ0 + .pkgi + .imp)"
[ "$n4" = "7" ] || bad "S=4: archive has $n4 members, expected 7 (four shard objects + the same three) — LOGOS_EMIT_SHARDS was not read"
[ "$n4w1" = "7" ] || bad "S=4/W=1: archive has $n4w1 members, expected 7"
note "members: S=1 -> $n1, S=4 -> $n4, S=4/W=1 -> $n4w1"

# ── A2. negative control: the knob CHANGED the artifact ────────────────────
nsh=$(grep -c '\.s[0-9]*\.o$' "$WORK/t4.txt" 2>/dev/null || true)
[ "${nsh:-0}" = "4" ] || bad "S=4: found ${nsh:-0} members named *.sN.o, expected 4 — the archive is not a sharded one"
if cmp -s "$A1" "$A4"; then
    bad "S=1 and S=4 archives are BYTE-IDENTICAL — the shard setting did nothing, and every equality this gate checks below would hold vacuously"
else
    note "negative control: S=1 and S=4 archives differ (the knob is live)"
fi

# ── F. worker count does not change the artifact ───────────────────────────
if cmp -s "$A4" "$A4W1"; then
    note "workers: S=4/W=4 and S=4/W=1 are byte-identical"
else
    bad "S=4/W=4 and S=4/W=1 archives DIFFER — the partition is supposed to be a pure function of the link names, so the worker pool leaked ordering or raced"
fi

# ── extract the members ────────────────────────────────────────────────────
X1="$WORK/x1"; X4="$WORK/x4"
mkdir -p "$X1" "$X4" || exit 1
( cd "$X1" && ar x "$A1" ) || bad "ar x failed on the S=1 archive"
( cd "$X4" && ar x "$A4" ) || bad "ar x failed on the S=4 archive"

OBJ1="$X1/shardgate.o"
[ -f "$OBJ1" ] || bad "S=1: no shardgate.o member in the archive"

SHARDS=()
for i in 0 1 2 3; do
    f="$X4/shardgate.o.s$i.o"
    if [ -f "$f" ]; then SHARDS+=("$f"); else bad "S=4: no shard member shardgate.o.s$i.o"; fi
done

# ── B. every shard member is at least 64 bytes ─────────────────────────────
# Restated outside the compiler so it holds even if emit_module's own guard is
# removed. ⚠ It catches TRUNCATION only — a bodyless shard object measured 496
# bytes, so this floor cannot see emptiness. Assertion C is what sees that.
for f in "${SHARDS[@]}"; do
    sz=$(stat -c %s "$f" 2>/dev/null || echo 0)
    [ "${sz:-0}" -ge 64 ] || bad "shard member $(basename "$f") is $sz bytes — not an object file whatever else it is"
done

if [ "${#SHARDS[@]}" = "4" ] && [ -f "$OBJ1" ]; then
    # ── C. the union of the shard objects defines exactly what one object did ──
    nm_pairs "$OBJ1" | cut -f2 | sort -u > "$WORK/names1.txt"
    : > "$WORK/pairs4.txt"
    for f in "${SHARDS[@]}"; do
        nm_pairs "$f" | sort -u >> "$WORK/pairs4.txt"
        per=$(nm_pairs "$f" | cut -f2 | sort -u | grep -c '' || true)
        # A per-shard floor: "the union matches" would also hold if one shard
        # took everything and three took nothing, which is not a partition.
        [ "${per:-0}" -ge 1 ] || bad "shard $(basename "$f") defines NO globals — the partition put nothing in it"
        note "  $(basename "$f"): ${per:-0} defined globals"
    done
    cut -f2 "$WORK/pairs4.txt" | sort -u > "$WORK/names4.txt"
    c1=$(count_lines "$WORK/names1.txt")
    c4=$(count_lines "$WORK/names4.txt")
    comm -23 "$WORK/names1.txt" "$WORK/names4.txt" > "$WORK/only1.txt"
    comm -13 "$WORK/names1.txt" "$WORK/names4.txt" > "$WORK/only4.txt"
    o1=$(count_lines "$WORK/only1.txt")
    o4=$(count_lines "$WORK/only4.txt")
    # The fixture floor. If nm returned nothing at all, "0 differences" would be
    # a pass over an empty comparison — the exact shape this gate exists to
    # refuse. 32 authored bodies plus their glue and vtables; the measured value
    # is in the thousands, so 100 is a floor on the DERIVATION, not a target.
    [ "${c1:-0}" -ge 100 ] || bad "the S=1 object defines only ${c1:-0} globals — nm examined essentially nothing, so every set comparison below is vacuous"
    [ "${o1:-0}" = "0" ] || {
        bad "$o1 name(s) defined at S=1 are defined by NO shard object — bodies vanished when the module was split:"
        head -5 "$WORK/only1.txt"
    }
    [ "${o4:-0}" = "0" ] || {
        bad "$o4 name(s) defined by a shard object are defined at NO single-object build — sharding invented definitions:"
        head -5 "$WORK/only4.txt"
    }
    note "defined globals: S=1 -> $c1, S=4 union -> $c4, only-S1 $o1, only-S4 $o4"

    # ── C2. nothing strong is defined twice ────────────────────────────────
    # ⚠ `pairs4.txt` IS DELIBERATELY NOT GLOBALLY `sort -u`-ED, and getting that
    # wrong is how this assertion first read 0 duplicates on an archive that had
    # four. Each shard contributes its OWN deduplicated (letter, name) lines, so
    # a name emitted by three shards appears three times. A `sort -u` across the
    # concatenation collapses exactly the repetition being counted — the check
    # then cannot fail, which is the shape this whole file exists to refuse.
    cut -f2 "$WORK/pairs4.txt" | sort | uniq -d > "$WORK/dups.txt"
    ndup=$(count_lines "$WORK/dups.txt")
    # letters: T text, D data, R rodata, B bss — all STRONG. W/V are weak
    # (LinkOnceODR / weak object), which is the whole point of the drop-glue
    # relinkage and is allowed to repeat.
    : > "$WORK/dupstrong.txt"
    if [ "${ndup:-0}" -gt 0 ]; then
        grep -F -f "$WORK/dups.txt" "$WORK/pairs4.txt" \
            | grep -E '^[TDRB]	' | sort -u > "$WORK/dupstrong.txt"
    fi
    nstrong=$(count_lines "$WORK/dupstrong.txt")
    [ "${nstrong:-0}" = "0" ] || {
        bad "$nstrong STRONG symbol(s) are defined in more than one shard object — one archive, two definitions, and the first consumer that pulls both members gets a link error:"
        head -5 "$WORK/dupstrong.txt"
    }
    # A FLOOR ON THE DUPLICATION ITSELF. `nstrong == 0` is also what an archive
    # with NO repeated symbol at all reports, and that archive would prove
    # nothing about linkage. The fixture guarantees repetition by construction:
    # `__drop_in_place__A/B` and `__logos_vtable__Sp__A/B` are synthesised in
    # every shard that touches the type. MEASURED at HEAD: 4 repeated names.
    [ "${ndup:-0}" -ge 4 ] || bad "only ${ndup:-0} name(s) are defined by more than one shard object — the fixture stopped producing cross-shard duplicates, so 'no STRONG duplicate' is a statement about an empty set"
    note "cross-shard duplicates: $ndup name(s), $nstrong of them strong"

    # ── D. drop glue is LinkOnceODR under sharding, and strong without it ───
    grep -P '^.\t__drop_in_place__' "$WORK/pairs4.txt" | sort -u > "$WORK/glue4.txt"
    nm_pairs "$OBJ1" | grep -P '^.\t__drop_in_place__' | sort -u > "$WORK/glue1.txt"
    gnames4=$(cut -f2 "$WORK/glue4.txt" | sort -u | grep -c '' || true)
    gnames1=$(cut -f2 "$WORK/glue1.txt" | sort -u | grep -c '' || true)
    # FIXTURE FLOOR, and it is not decoration: the fixture defines exactly two
    # concrete types behind `dyn Sp`, both with a `String` field, so there are at
    # least `__drop_in_place__A` and `__drop_in_place__B`. If this were 0 the two
    # letter assertions below would examine nothing and report clean.
    [ "${gnames4:-0}" -ge 2 ] || bad "only ${gnames4:-0} distinct __drop_in_place__ name(s) in the shard objects — the fixture stopped generating drop glue, so the linkage assertion is examining nothing"
    [ "${gnames1:-0}" -ge 2 ] || bad "only ${gnames1:-0} distinct __drop_in_place__ name(s) in the S=1 object"
    nbadglue=$(grep -vP '^W\t' "$WORK/glue4.txt" | grep -c '' || true)
    [ "${nbadglue:-0}" = "0" ] || {
        bad "$nbadglue drop-glue definition(s) in a shard object are not W (LinkOnceODR) — every shard that drops a T emits its own copy, so a strong one is a duplicate-definition link error:"
        head -5 "$WORK/glue4.txt"
    }
    nbadglue1=$(grep -vP '^T\t' "$WORK/glue1.txt" | grep -c '' || true)
    [ "${nbadglue1:-0}" = "0" ] || {
        bad "$nbadglue1 drop-glue definition(s) in the SINGLE-object build are not T — the relinkage is supposed to apply only when sharding, so the unsharded path stays byte-identical and the ABI does not move:"
        head -5 "$WORK/glue1.txt"
    }
    note "drop glue: $gnames4 name(s) in shards (all W), $gnames1 in the single object (all T)"
fi

# ── E. the consumer links and prints the hand-derived answer ───────────────
# ⚠ NOT A DETECTOR. Kept because a duplicated STRONG symbol becomes a link error
# here and nowhere else, and because it is cheap. It passed with every shard
# object empty (CONTROL 1b), so it is never the reason this gate is green.
# The link deliberately omits `-Wl,--allow-multiple-definition`, which
# run_test.sh passes for every corpus test: that flag turns the duplicate-symbol
# failure mode into a silent one, and this is the one place we want it loud.
link_and_run() {  # link_and_run <archive> <tag>
    local ar_path="$1" tag="$2"
    local obj="$WORK/use_$tag.o" bin="$WORK/use_$tag"
    if ! ( cd "$REPO" && TMPDIR="$WORK/tmp" "$LOGOSC" tests/logos/shardgate_use/use.logos \
            -L "$LIBDIR" -l "$ar_path" -o "$obj" ) > "$WORK/use_$tag.clog" 2>&1; then
        bad "consumer failed to compile against $tag:"; tail -5 "$WORK/use_$tag.clog"; return
    fi
    local archives=()
    for a in "$LIBDIR"/liblstdlib*.a; do [ -f "$a" ] && archives+=("$a"); done
    for a in "$LIBDIR"/liblogos-*.a; do [ -f "$a" ] && archives+=("$a"); done
    archives+=("$ar_path")
    if ! cc "$obj" -Wl,--start-group "${archives[@]}" -Wl,--end-group \
            -lpthread -lm -lstdc++ -o "$bin" > "$WORK/use_$tag.llog" 2>&1; then
        bad "consumer failed to LINK against $tag (a duplicate strong definition in the archive lands here):"
        grep -m5 -E 'multiple definition|undefined reference' "$WORK/use_$tag.llog" || tail -5 "$WORK/use_$tag.llog"
        return
    fi
    local out rc
    out=$("$bin" 2>/dev/null); rc=$?
    [ "$rc" = "0" ] || bad "consumer against $tag exited $rc"
    # 360 = sum(i, i=0..15) + sum(2i, i=0..15) = 120 + 240, derived by hand.
    [ "$out" = "SUM=360" ] || bad "consumer against $tag printed '$out', expected 'SUM=360'"
    note "consumer against $tag: rc=$rc out='$out'"
}
link_and_run "$A1" "s1"
link_and_run "$A4" "s4"

if [ "$fail" != "0" ]; then
    echo "emit_shards_gate: FAILED"
    exit 1
fi
echo "emit_shards_gate: OK"
