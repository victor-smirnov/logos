#!/usr/bin/env bash
# WQL/Trama PARSER REGENERATION (ADR 0012 peg-frontend, Phase 4).
#
# Regenerates the CHECKED-IN generated parsers from their grammars:
#   stdlib/mem/wql/grammars/el.peg    -> stdlib/mem/wql/el_parser.logos
#   stdlib/mem/wql/grammars/trama.peg -> stdlib/mem/wql/trama_parser.logos
#   stdlib/mem/wql/grammars/wql.peg   -> stdlib/mem/wql/wql_surface_parser.logos
#
# This is an OFFLINE, opt-in step (target `wql_peg_regen`). The generated parsers
# are committed source artifacts; a normal `cmake --build` compiles the committed
# .logos and NEVER runs the generator — so there is NO build cycle (peg_gen_logos
# links liblogos-std, which contains the generated parsers). Regeneration is done
# by hand when a grammar changes, mirroring how logos_parser.logos is regenerated
# offline and gated by the peg_gen_logos_oracle.
#
# After regenerating, rebuild (cmake --build) and run the behavior oracle
# (wql_peg_oracle) + the scoped ctest (wql|trama) to gate the change.
set -eu

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/../../.." && pwd)"
build="${1:-$repo/build}"

peg="$build/bin/peg_gen_logos"
grammars="$repo/stdlib/mem/wql/grammars"
outdir="$repo/stdlib/mem/wql"

[ -x "$peg" ] || { echo "missing $peg — build peg_gen_logos first (cmake --build $build --target peg_gen_logos)"; exit 1; }

echo "== regenerating el_parser.logos from el.peg =="
"$peg" "$grammars/el.peg" --out-dir "$outdir"

echo "== regenerating trama_parser.logos from trama.peg =="
"$peg" "$grammars/trama.peg" --out-dir "$outdir"

echo "== regenerating wql_surface_parser.logos from wql.peg =="
"$peg" "$grammars/wql.peg" --out-dir "$outdir"

echo "----------------------------------------"
echo "regenerated:"
echo "  $outdir/el_parser.logos"
echo "  $outdir/trama_parser.logos"
echo "  $outdir/wql_surface_parser.logos"
echo "Now: cmake --build $build && cmake --build $build --target wql_peg_oracle"
