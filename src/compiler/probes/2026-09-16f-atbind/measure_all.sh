#!/usr/bin/env bash
# measure_all.sh <logosc> <tag> — run the three measured sets against one binary.
LOGOSC="$1"; TAG="$2"
D=$(dirname "$0")
export LOGOS_LIB_DIR=/home/logos/devel/logos/build/lib/logos
cd "$D" && ls b*.logos | xargs -P8 -I{} "$D/runone.sh" "$LOGOSC" {} | sort > "$D/${TAG}_HB.tsv"
cd /home/logos/devel/logos/src/compiler/probes/2026-09-16e-atbind/battery && ls a*.logos | xargs -P8 -I{} "$D/runone.sh" "$LOGOSC" {} | sort > "$D/${TAG}_PRICING_A.tsv"
cd /home/logos/devel/logos/tests/soundness/open && for r in at_binding_aggregate_segfaults_run at_binding_by_value_never_dropped at_binding_ref_mut_payload_refuses let_at_binding_tuple_sub_unsupported let_at_binding_type_annot_syntax at_binding_whole_struct_wild_fields_refused at_binding_array_sub_verifier_error_refused declare_pat_bindings_field_nomut_addrmut_admit; do echo "$r.logos"; done | xargs -P8 -I{} "$D/runone.sh" "$LOGOSC" {} | sort > "$D/${TAG}_ATROWS.tsv"
echo "--- $TAG HB ---"; cat "$D/${TAG}_HB.tsv"
echo "--- $TAG PRICING ---"; cat "$D/${TAG}_PRICING_A.tsv"
echo "--- $TAG ATROWS ---"; cat "$D/${TAG}_ATROWS.tsv"
