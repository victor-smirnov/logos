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
LINT_BASELINE=10
# 10 — bumped 2026-05-19 for mono Phase 2: eager blanket loop builds
# candidate TypeRefs up-front so the extra-bound check can route through
# `mono_concrete_satisfies_bound` (deep) instead of
# `mono_has_impl_recursive` (shallow). Two new inline LogosTypeBuilder
# sites in `mono.cpp` mirror the existing build pattern at the bottom
# of the candidate loop. Factor candidates: lift the existing
# per-method build to the candidate level (single shared TypeRef per
# candidate) — folded into Phase 2 worklist refactor.

count=$(grep -rE "kind = LogosType::Kind::(Struct|Enum|ZonedStruct)\b" \
        "$ROOT/src/compiler/" 2>/dev/null \
        | grep -v "sema_impl.hpp" \
        | wc -l)

if [ "$count" -gt "$LINT_BASELINE" ]; then
    echo "ERROR: inline LogosTypeBuilder Struct/Enum/ZonedStruct sites grew" >&2
    echo "  baseline: $LINT_BASELINE, current: $count" >&2
    echo "Each new site is a pkg-threading risk.  Use make_*_type helpers." >&2
    grep -rEn "kind = LogosType::Kind::(Struct|Enum|ZonedStruct)\b" \
        "$ROOT/src/compiler/" | grep -v "sema_impl.hpp" >&2
    exit 1
fi

if [ "$count" -lt "$LINT_BASELINE" ]; then
    echo "INFO: lint baseline can be ratcheted down ($LINT_BASELINE → $count)"
fi
exit 0
