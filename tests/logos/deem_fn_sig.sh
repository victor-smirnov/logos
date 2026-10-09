# deem_fn_sig.sh — sourced, not run: THE one spelling of a Deem-generated fn's
# head line, for every gate that reads a `--gen-dir` dump.
#
# Six census gates used to match `pub fn <q>_run(` by FOUR hand-written
# spellings; making the emitter write the query's region (`pub fn q_run<'a>(`)
# orphaned all of them at once and seven gates went red for a reason unrelated
# to what they measure (unrowed_backlog: run_signature_matched_by_hand_six_places).
# A change to the emitted signature now reaches every reader here.

# deem_fn_re NAME — the ERE of fn NAME's head line, up to and including its `(`.
# NAME may itself be an ERE fragment (`q._run`, `[a-z_]+_run`).
deem_fn_re() { printf '^pub fn %s(<[^>]*>)?\\(' "$1"; }

# deem_fn_count FILE NAME — how many definitions of NAME FILE holds.
deem_fn_count() { grep -cE "$(deem_fn_re "$2")" "$1" || true; }

# deem_fn_body FILE NAME — fn NAME's lines, head to the first column-0 `}`;
# false when there is none. (A `done` flag, not awk's `exit`: one meaning of
# the word in a gate.)
deem_fn_body() {
    local n
    n=$(grep -nE "$(deem_fn_re "$2")" "$1" | head -1 | cut -d: -f1)
    [ -n "$n" ] || return 1
    awk -v s="$n" '
        !done && NR >= s { print }
        !done && NR >= s && $0 == "}" { done = 1 }
    ' "$1"
}
