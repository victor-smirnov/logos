#!/usr/bin/env bash
# Lint: detect inline `LogosTypeBuilder` direct kind assignments to
# Struct / Enum / ZonedStruct outside the canonical helper file
# (sema_impl.hpp).  Each such site is an opportunity to forget pkg-threading;
# preferred form is `make_struct_type` / `make_enum_type` / `make_generic_*`.
#
# A baseline of known-existing sites is allowed; any *new* site fails the
# lint.  Update LINT_BASELINE only after confirming each remaining site
# correctly threads pkg_name.
#
# See:
#   memory/antipat_inline_typebuilder.md
#   memory/feat_type_uid_pkg_skip_bug.md
#   docs/baghunt/categorization.md (Cluster: ad-hoc-typebuilder)

set -euo pipefail
ROOT="${1:-$(dirname "$(realpath "$0")")/..}"
LINT_BASELINE=5
# 5 — ratcheted down 2026-07-11: mono_clone's three metacall-instantiation
# inline builders + the partial-spec seam consolidated into
# `Mono::build_generic_struct_typeref` (helper counts as 1). The grep is
# now whitespace-robust (aligned `kind        =` used to evade it).
# 7 — ratcheted down 2026-05-19 after mono Phase 2 step 2 introduced
# `Mono::build_concrete_typeref(const std::string& name)` in
# `mono_impl.hpp` and deduped the three inline Struct/Enum/primitive
# TypeRef builds in mono.cpp's eager blanket loop down to one helper
# call per candidate. Helper itself counts as 1 site (Enum branch);
# net reduction 10 → 7.

# ⚠ THE POPULATION IS `src/`, NOT `src/compiler/`. A lint whose scan is one
# hand-named directory says nothing about the same anti-pattern one directory
# over: `LogosTypeBuilder` is reachable from every TU that includes sema.hpp, and
# `src/` holds seven of them (compiler, core, hrpc, jit, reactor, verification,
# writ). MEASURED 2026-08-01 at the widening: 0 sites outside src/compiler, so
# the baseline is unchanged — which is the point. The number that would have had
# to change is the one this scan could not see.
count=$(grep -rE "kind[[:space:]]*=[[:space:]]*LogosType::Kind::(Struct|Enum|ZonedStruct)\b" \
        "$ROOT/src/" "$ROOT/tools/" 2>/dev/null \
        | grep -v "sema_impl.hpp" \
        | wc -l)

if [ "$count" -gt "$LINT_BASELINE" ]; then
    echo "ERROR: inline LogosTypeBuilder Struct/Enum/ZonedStruct sites grew" >&2
    echo "  baseline: $LINT_BASELINE, current: $count" >&2
    echo "Each new site is a pkg-threading risk.  Use make_*_type helpers." >&2
    grep -rEn "kind[[:space:]]*=[[:space:]]*LogosType::Kind::(Struct|Enum|ZonedStruct)\b" \
        "$ROOT/src/" "$ROOT/tools/" | grep -v "sema_impl.hpp" >&2
    exit 1
fi

if [ "$count" -lt "$LINT_BASELINE" ]; then
    echo "INFO: lint baseline can be ratcheted down ($LINT_BASELINE → $count)"
fi
exit 0
