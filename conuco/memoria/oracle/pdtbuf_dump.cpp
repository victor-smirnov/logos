// pdtbuf_dump — C++ PackedDataTypeBuffer cross-check reference for the Logos
// PdtBuf port (ladder rungs P0 + P1 + P2).
//
// ROLE (read this before trusting a byte of the output): this harness is a
// DIVERGENCE DETECTOR, not an authority. The C++ PDTBuffer/FQTree family is
// dense with bugs (26 catalogued through rung P1; three of them are exercised
// below on purpose), and the FQTree half of the lineage was deleted upstream
// without ever having been covered by a test suite. Correctness in the Logos
// port is decided by the independent materialized model plus algebraic laws
// in tests/pdtbuf_core.logos; these fixtures exist so that any disagreement
// with HEAD becomes visible and has to be explained.
//
// Instantiates PackedDataTypeBufferT<BigInt, true, C, DTOrdering::SUM> and the
// DTOrdering::MAX variant for C in {1,2,3,4}, fills them through
// insert_from_fn, and dumps (corpus, query) -> (answer) triples for
// access / sum / find_fw_ge / find_fw_gt. Row counts stay at or below
// IndexSpan (32), so HEAD never materializes its span index and its answers
// are its pure scan/bisection paths — exactly the paths P0 implements.
//
// Build + regenerate (from conuco/memoria/; see oracle/README.md):
//
//   g++ -std=c++20 -O1 -DMMA_ICU_CXX_NS=icu_74 \
//       -I $HOME/cxx/memoria/core/include \
//       -I $HOME/cxx/memoria/containers/include \
//       -I $HOME/cxx/memoria/containers-api/include \
//       -I $HOME/cxx/memoria/build/Clang_19-Debug/vcpkg_installed/x64-linux/include \
//       oracle/pdtbuf_dump.cpp -o /tmp/pdtbuf_dump \
//       $HOME/cxx/memoria/build/Clang_19-Debug/mbt-explicit/libCore.a \
//       $HOME/cxx/memoria/build/Clang_19-Debug/vcpkg_installed/x64-linux/lib/libfmt.a \
//       -licuuc -licudata \
//   && /tmp/pdtbuf_dump > tests/pdtbuf_fixtures.hex
//
// Output format (whitespace-separated lowercase hex tokens, no 0x). A case:
//
//   1 <ordering> <columns> <rows>          ordering: 0 = SUM, 1 = MAX
//   <v> ...                                rows*columns values, ROW-MAJOR
//   <n_sum>                                SUM cases only (0 for MAX)
//     <col> <start> <end> <sum>            x n_sum
//   <n_find>
//     <op> <col> <val> <idx> <prefix>      x n_find; op 0 = ge, 1 = gt
//   <n_gen>                                MAX cases only (0 for SUM)
//     <col> <row> <cpp_sum_at_row>         x n_gen  -- the sum_gen DIVERGENCE
//
// terminated by a single `0` token.
//
// The <n_gen> block is deliberate evidence, not a fixture we conform to:
// for MAX/UNORDERED buffers the C++ routes sum(column,row) to sum_gen
// (so.hpp:1577), which sums [row, size) -- a SUFFIX -- while the SUM arm
// sum_sum (:1599) sums [0, row) -- a PREFIX. The very next C++ method,
// sum(column, start, end) = sum(end) - sum(start) (:889), is therefore the
// NEGATION of the true range sum on any MAX buffer. The Logos port
// implements prefix semantics for every ordering (the only reading under
// which the documented identity holds) and the Logos test asserts the
// divergence explicitly as cpp_sum_gen(col,row) == total(col) - prefix(col,row).
//
// No time, no unseeded randomness: the corpus is a pure function of this
// file. Every case is self-checked here (C++ access == the source vector;
// C++ sum == a naive loop; C++ find == a naive scan) BEFORE emission, and
// the two self-checks that C++ FAILS are noted below rather than silenced.

#include <memoria/core/packed/tools/packed_struct_ptrs.hpp>
#include <memoria/core/packed/datatype_buffer/packed_datatype_buffer.hpp>
#include <memoria/core/datatypes/varchars/varchar_dt.hpp>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <string>
#include <algorithm>

using namespace memoria;

namespace {

// Knuth MMIX LCG — the same constants the conuco tests use.
struct Lcg {
    uint64_t s;
    explicit Lcg(uint64_t seed): s(seed) {}
    uint64_t next() {
        s = s * 6364136223846793005ull + 1442695040888963407ull;
        return s >> 33;
    }
};

size_t g_cases = 0;
size_t g_failed_selfchecks = 0;

void tok(uint64_t v) {
    std::printf("%llx ", (unsigned long long)v);
}

// The row counts the corpus sweeps: empty, singletons, small, and both sides
// of every boundary that matters at this rung (the IndexSpan threshold 32 is
// the last one — above it HEAD switches to its index path, which is P1).
const size_t ROW_SET[] = {0, 1, 2, 3, 5, 8, 15, 16, 17, 31, 32};

template <size_t Columns, DTOrdering Ordering>
void dump_case(size_t rows, uint64_t seed)
{
    using Buf = PackedDataTypeBufferT<BigInt, true, Columns, Ordering>;

    Lcg lcg(seed);

    // Corpus. Values stay small and non-negative: the Logos port narrows
    // cells to u64 at this rung, and negative keys have no meaning for
    // either ordering's descent.
    std::vector<std::vector<int64_t>> cols(Columns);
    for (size_t c = 0; c < Columns; c++) {
        for (size_t i = 0; i < rows; i++) {
            cols[c].push_back((int64_t)(lcg.next() % 23));
        }
        if (Ordering == DTOrdering::MAX) {
            // A MAX column is a SORTED key column by construction — that is
            // what makes its search a bisection and why HEAD carries no index
            // for it. Duplicates are kept: they are exactly where lower_bound
            // and upper_bound part ways.
            std::sort(cols[c].begin(), cols[c].end());
        }
    }

    auto holder = PkdStructHolder<Buf>::make_empty(1024 * 64);
    auto so = holder->get_so();
    if (rows > 0) {
        so.insert_from_fn(0, rows, [&](size_t column, size_t row) {
            return cols[column][row];
        });
    }

    // Self-check: access round-trips the corpus.
    if (so.size() != rows) {
        std::fprintf(stderr, "SELFCHECK size mismatch\n");
        g_failed_selfchecks++;
    }
    for (size_t c = 0; c < Columns; c++) {
        for (size_t i = 0; i < rows; i++) {
            if ((int64_t)so.access(c, i) != cols[c][i]) {
                std::fprintf(stderr, "SELFCHECK access mismatch\n");
                g_failed_selfchecks++;
            }
        }
    }

    tok(1);
    tok(Ordering == DTOrdering::SUM ? 0 : 1);
    tok(Columns);
    tok(rows);
    std::printf("\n");
    for (size_t i = 0; i < rows; i++) {
        for (size_t c = 0; c < Columns; c++) {
            tok((uint64_t)cols[c][i]);
        }
    }
    std::printf("\n");

    auto naive_prefix = [&](size_t c, size_t k) {
        int64_t s = 0;
        for (size_t i = 0; i < k; i++) {
            s += cols[c][i];
        }
        return s;
    };

    // ── sum queries (SUM ordering only) ───────────────────────────────────
    // Not emitted for MAX: HEAD's sum there is the sum_gen suffix bug, and a
    // fixture we know to be wrong is not a fixture, it is a trap. The <n_gen>
    // block below records that behaviour as evidence instead.
    std::vector<std::array<uint64_t, 4>> sums;
    if (Ordering == DTOrdering::SUM) {
        // Exhaustive over every (start,end) pair while that is cheap; above
        // 8 rows the pair space is sampled on the boundary set (the laws in
        // the Logos gate cover the interior exhaustively on their own model,
        // so the fixture only has to be dense where conventions live).
        std::vector<size_t> cuts;
        if (rows <= 8) {
            for (size_t k = 0; k <= rows; k++) {
                cuts.push_back(k);
            }
        } else {
            size_t cand[] = {0, 1, 2, rows / 2 - 1, rows / 2, rows / 2 + 1,
                             rows - 2, rows - 1, rows};
            for (size_t k: cand) {
                if (k <= rows) {
                    cuts.push_back(k);
                }
            }
            std::sort(cuts.begin(), cuts.end());
            cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());
        }
        for (size_t c = 0; c < Columns; c++) {
            for (size_t start: cuts) {
                for (size_t end: cuts) {
                    if (end < start) {
                        continue;
                    }
                    int64_t got = so.sum(c, start, end);
                    int64_t want = naive_prefix(c, end) - naive_prefix(c, start);
                    if (got != want) {
                        std::fprintf(stderr, "SELFCHECK sum mismatch c=%zu [%zu,%zu)\n",
                                     c, start, end);
                        g_failed_selfchecks++;
                    }
                    sums.push_back({(uint64_t)c, (uint64_t)start, (uint64_t)end,
                                    (uint64_t)got});
                }
            }
        }
    }
    tok(sums.size());
    std::printf("\n");
    for (auto& s: sums) {
        tok(s[0]);
        tok(s[1]);
        tok(s[2]);
        tok(s[3]);
    }
    std::printf("\n");

    // ── find queries ──────────────────────────────────────────────────────
    // Query values sweep every attainable answer plus both edges: 0, each
    // reachable prefix (SUM) / each present key (MAX), the total, total+1.
    std::vector<uint64_t> probes;
    probes.push_back(0);
    for (size_t c = 0; c < Columns; c++) {
        if (Ordering == DTOrdering::SUM) {
            for (size_t k = 0; k <= rows; k++) {
                int64_t p = naive_prefix(c, k);
                probes.push_back((uint64_t)p);
                if (p > 0) {
                    probes.push_back((uint64_t)(p - 1));
                }
                probes.push_back((uint64_t)(p + 1));
            }
        } else {
            for (size_t i = 0; i < rows; i++) {
                probes.push_back((uint64_t)cols[c][i]);
                if (cols[c][i] > 0) {
                    probes.push_back((uint64_t)(cols[c][i] - 1));
                }
                probes.push_back((uint64_t)(cols[c][i] + 1));
            }
        }
    }
    std::sort(probes.begin(), probes.end());
    probes.erase(std::unique(probes.begin(), probes.end()), probes.end());

    std::vector<std::array<uint64_t, 5>> finds;
    for (size_t c = 0; c < Columns; c++) {
        for (uint64_t v: probes) {
            for (int op = 0; op < 2; op++) {
                auto r = (op == 0) ? so.find_fw_ge(c, (int64_t)v)
                                   : so.find_fw_gt(c, (int64_t)v);
                uint64_t idx = (uint64_t)r.local_pos();
                uint64_t pfx = (uint64_t)(int64_t)r.prefix();

                // Self-check against a naive scan derived from the DEFINITION
                // of the operation, not from the C++ implementation:
                //   SUM: first i with prefix(i+1) >= v  (ge) / > v (gt)
                //   MAX: first i with col[i] >= v (ge) / > v (gt)
                uint64_t want_idx = rows;
                uint64_t want_pfx = 0;
                if (Ordering == DTOrdering::SUM) {
                    int64_t p = 0;
                    size_t i = 0;
                    for (; i < rows; i++) {
                        int64_t incl = p + cols[c][i];
                        bool hit = (op == 0) ? (incl >= (int64_t)v) : (incl > (int64_t)v);
                        if (hit) {
                            break;
                        }
                        p = incl;
                    }
                    want_idx = i;
                    want_pfx = (uint64_t)p;
                } else {
                    size_t i = 0;
                    for (; i < rows; i++) {
                        bool hit = (op == 0) ? (cols[c][i] >= (int64_t)v)
                                             : (cols[c][i] > (int64_t)v);
                        if (hit) {
                            break;
                        }
                    }
                    want_idx = i;
                    want_pfx = 0;
                }
                if (idx != want_idx || pfx != want_pfx) {
                    std::fprintf(stderr,
                        "SELFCHECK find mismatch ord=%d C=%zu rows=%zu c=%zu op=%d v=%llu"
                        " cpp=(%llu,%llu) naive=(%llu,%llu)\n",
                        (int)Ordering, Columns, rows, c, op, (unsigned long long)v,
                        (unsigned long long)idx, (unsigned long long)pfx,
                        (unsigned long long)want_idx, (unsigned long long)want_pfx);
                    g_failed_selfchecks++;
                }
                finds.push_back({(uint64_t)op, (uint64_t)c, v, idx, pfx});
            }
        }
    }
    tok(finds.size());
    std::printf("\n");
    for (auto& f: finds) {
        tok(f[0]);
        tok(f[1]);
        tok(f[2]);
        tok(f[3]);
        tok(f[4]);
        std::printf("\n");
    }

    // ── the sum_gen divergence record (MAX ordering only) ─────────────────
    std::vector<std::array<uint64_t, 3>> gens;
    if (Ordering == DTOrdering::MAX) {
        for (size_t c = 0; c < Columns; c++) {
            for (size_t k = 0; k <= rows; k++) {
                gens.push_back({(uint64_t)c, (uint64_t)k,
                                (uint64_t)(int64_t)so.sum(c, k)});
            }
        }
    }
    tok(gens.size());
    std::printf("\n");
    for (auto& g: gens) {
        tok(g[0]);
        tok(g[1]);
        tok(g[2]);
    }
    std::printf("\n");

    g_cases++;
}

// ── index-cell parity cases (marker 2; PdtBuf rung P1) ──────────────────────
//
// Row counts ABOVE IndexSpan, where HEAD materializes its recursive index
// (`using IndexType = MyType` — packed_datatype_buffer.hpp:65). Dumps the
// index CELLS level by level, walking `so.index()` until has_index() stops:
// the strict gate for our own levels, cell for cell.
//
// The CORPUS is not dumped. It is a pure function of (seed, Columns, rows)
// through the same LCG, in the same column-major fill order, that the Logos
// gate runs — so both sides regenerate it identically and a 32769-row case
// costs a few KB of index cells instead of a megabyte of corpus.
//
//   2 <columns> <rows> <seed>
//   <n_levels>
//     <level_rows> <cells ...>       x n_levels, cells ROW-MAJOR
//
// Self-checked from the DEFINITION before emission: level 1 cell (c,k) is the
// PARTIAL sum of corpus rows [k*32, min((k+1)*32, rows)) — not a prefix — and
// level L+1 is the same rule applied to level L's cells.
const size_t DEEP_ROW_SET[] = {33, 64, 65, 100, 200, 1023, 1024, 1025, 2000, 32769};

const size_t SPAN = 32;

// P4 (marker 5/6) row counts and multi-row update widths.
const size_t VLE_ROW_SET[] = {0, 1, 2, 5, 9, 17};

template <size_t Columns>
void dump_index_case(size_t rows, uint64_t seed)
{
    using Buf = PackedDataTypeBufferT<BigInt, true, Columns, DTOrdering::SUM>;

    Lcg lcg(seed);
    std::vector<std::vector<int64_t>> cols(Columns);
    for (size_t c = 0; c < Columns; c++) {
        for (size_t i = 0; i < rows; i++) {
            cols[c].push_back((int64_t)(lcg.next() % 23));
        }
    }

    auto holder = PkdStructHolder<Buf>::make_empty(
        Buf::compute_block_size(rows) + 64 * 1024);
    auto so = holder->get_so();
    so.insert_from_fn(0, rows, [&](size_t column, size_t row) {
        return cols[column][row];
    });

    if (so.size() != rows) {
        std::fprintf(stderr, "SELFCHECK index-case size mismatch\n");
        g_failed_selfchecks++;
    }
    // HEAD's own structural check, which for SUM buffers walks the index and
    // compares it against a recomputed span sum (so.hpp check_sum :1649).
    so.check();

    // Walk the levels.
    std::vector<size_t> level_rows;
    std::vector<std::vector<uint64_t>> levels;
    {
        auto cur = so;
        while (cur.data()->has_index()) {
            auto ix = cur.index();
            size_t n = ix.size();
            std::vector<uint64_t> cells;
            for (size_t r = 0; r < n; r++) {
                for (size_t c = 0; c < Columns; c++) {
                    cells.push_back((uint64_t)(int64_t)ix.access(c, r));
                }
            }
            level_rows.push_back(n);
            levels.push_back(std::move(cells));
            cur = ix;
        }
    }

    // Self-check: rebuild every level from the definition.
    {
        std::vector<std::vector<int64_t>> below = cols;
        for (size_t L = 0; L < levels.size(); L++) {
            size_t n_below = below[0].size();
            size_t spans = (n_below + SPAN - 1) / SPAN;
            if (spans != level_rows[L]) {
                std::fprintf(stderr,
                    "SELFCHECK index level %zu row count: cpp=%zu naive=%zu\n",
                    L, level_rows[L], spans);
                g_failed_selfchecks++;
            }
            std::vector<std::vector<int64_t>> up(Columns);
            for (size_t k = 0; k < spans; k++) {
                size_t limit = (k + 1) * SPAN < n_below ? (k + 1) * SPAN : n_below;
                for (size_t c = 0; c < Columns; c++) {
                    int64_t s = 0;
                    for (size_t i = k * SPAN; i < limit; i++) {
                        s += below[c][i];
                    }
                    up[c].push_back(s);
                    if (k < level_rows[L]
                        && (uint64_t)s != levels[L][k * Columns + c]) {
                        std::fprintf(stderr,
                            "SELFCHECK index cell L=%zu c=%zu k=%zu cpp=%llu naive=%lld\n",
                            L, c, k,
                            (unsigned long long)levels[L][k * Columns + c],
                            (long long)s);
                        g_failed_selfchecks++;
                    }
                }
            }
            below = up;
        }
        // The level count itself: HEAD stops when a level fits in one span.
        size_t want_levels = 0;
        for (size_t n = rows; n > SPAN; n = (n + SPAN - 1) / SPAN) {
            want_levels++;
        }
        if (want_levels != levels.size()) {
            std::fprintf(stderr, "SELFCHECK level count cpp=%zu naive=%zu (rows=%zu)\n",
                         levels.size(), want_levels, rows);
            g_failed_selfchecks++;
        }
    }

    tok(2);
    tok(Columns);
    tok(rows);
    tok(seed);
    std::printf("\n");
    tok(levels.size());
    std::printf("\n");
    for (size_t L = 0; L < levels.size(); L++) {
        tok(level_rows[L]);
        for (uint64_t v: levels[L]) {
            tok(v);
        }
        std::printf("\n");
    }

    g_cases++;
}

// ── direction cases (marker 3; PdtBuf rung P2) ──────────────────────────────
//
// The start-relative and BACKWARD search families. Like marker 2 the corpus is
// not dumped — it is a pure function of (mode, seed, Columns, rows) that the
// Logos gate regenerates — and like marker 2 the row counts straddle the span
// threshold so HEAD is exercised on BOTH its scan path and its index path.
//
//   3 <columns> <rows> <seed> <mode>
//   <n_q>
//     <dir> <op> <col> <start> <val> <idx> <prefix>    x n_q
//   <n_div>
//     <dir> <op> <col> <start> <val> <status> <idx> <prefix>   x n_div
//
//   dir 0 = findGEForward/findGTForward(column, start, val)  (start in [0,rows])
//   dir 1 = findGEBackward/findGTBackward(column, start, val) (start in [0,rows))
//   dir 2 = findGEBackward/findGTBackward(column, val)        (start token 0)
//   op  0 = GE, 1 = GT
//   mode 0 = LCG values mod 23; mode 1 = a leading-zero PLATEAU corpus
//   status (divergence block only) 0 = HEAD returned a disagreeing answer,
//                                  1 = HEAD's own assertion would fire, so
//                                      the query was NOT executed (idx and
//                                      prefix are 0 and carry no meaning)
//
// Status 1 is not squeamishness. MEMORIA_ASSERT (core/tools/assert.hpp:38)
// catches its own throw and calls std::terminate(), so a tripped assertion
// takes the whole process down and cannot be observed any other way; the
// affected queries are identified by an independent computation (see
// terminates_head below) and reported rather than run.
//
// The <n_q> block holds only the queries where HEAD AGREES with a naive
// implementation derived from the definition; those are cross-check fixtures.
// The <n_div> block holds the ones where it does not, and the Logos gate
// asserts the DISAGREEMENT rather than conforming to it — the same treatment
// the MAX sum_gen block gets. Every entry of <n_div> is a face of upstream bug
// candidate #27 (see the divergence note in oracle/README.md).

// mode 1: leading zeros, then a small repeating pattern. A prefix PLATEAU is
// the only shape under which a rebase/complement target is attained by more
// than one row index, which is exactly where #27 lives.
int64_t plateau_value(size_t c, size_t r, size_t rows)
{
    if (r >= rows / 3) {
        return (int64_t)(1 + ((r + c) % 5));
    }
    return 0;
}

const size_t DIR_ROW_SET[] = {0, 1, 5, 31, 32, 33, 64, 65, 100, 1025};

template <size_t Columns>
void dump_dir_case(size_t rows, uint64_t seed, int mode)
{
    using Buf = PackedDataTypeBufferT<BigInt, true, Columns, DTOrdering::SUM>;

    Lcg lcg(seed);
    std::vector<std::vector<int64_t>> cols(Columns);
    for (size_t c = 0; c < Columns; c++) {
        for (size_t i = 0; i < rows; i++) {
            cols[c].push_back(mode == 0 ? (int64_t)(lcg.next() % 23)
                                        : plateau_value(c, i, rows));
        }
    }

    auto holder = PkdStructHolder<Buf>::make_empty(
        Buf::compute_block_size(rows ? rows : 1) + 64 * 1024);
    auto so = holder->get_so();
    if (rows > 0) {
        so.insert_from_fn(0, rows, [&](size_t column, size_t row) {
            return cols[column][row];
        });
    }
    if (so.size() != rows) {
        std::fprintf(stderr, "SELFCHECK dir-case size mismatch\n");
        g_failed_selfchecks++;
    }

    auto range = [&](size_t c, size_t s, size_t e) {
        int64_t acc = 0;
        for (size_t i = s; i < e; i++) {
            acc += cols[c][i];
        }
        return acc;
    };

    // The DEFINITIONS. Written as the walks themselves, not as any identity
    // the C++ (or the port) uses to compute them.
    auto naive_from = [&](size_t c, size_t start, uint64_t v, bool strict) {
        int64_t p = 0;
        for (size_t i = start; i < rows; i++) {
            int64_t incl = p + cols[c][i];
            bool hit = strict ? (incl > (int64_t)v) : (incl >= (int64_t)v);
            if (hit) {
                return std::array<uint64_t, 2>{(uint64_t)i, (uint64_t)p};
            }
            p = incl;
        }
        return std::array<uint64_t, 2>{(uint64_t)rows, (uint64_t)p};
    };
    auto naive_bw = [&](size_t c, size_t end, uint64_t v, bool strict) {
        int64_t acc = 0;
        for (size_t i = end; i > 0; ) {
            i--;
            int64_t incl = acc + cols[c][i];
            bool hit = strict ? (incl > (int64_t)v) : (incl >= (int64_t)v);
            if (hit) {
                return std::array<uint64_t, 2>{(uint64_t)i, (uint64_t)acc};
            }
            acc = incl;
        }
        return std::array<uint64_t, 2>{(uint64_t)end, (uint64_t)acc};
    };

    // The global forward walk, from the definition. Used only to PREDICT
    // which backward queries would take HEAD's process down (below).
    auto naive_fw = [&](size_t c, uint64_t v, bool strict) {
        int64_t p = 0;
        for (size_t i = 0; i < rows; i++) {
            int64_t incl = p + cols[c][i];
            bool hit = strict ? (incl > (int64_t)v) : (incl >= (int64_t)v);
            if (hit) {
                return i;
            }
            p = incl;
        }
        return rows;
    };

    // WOULD HEAD TERMINATE? find_ge_bw_sum (so.hpp:1445-1460) takes the
    // NON-STRICT branch whenever val <= total, delegates to find_gt_fw_sum,
    // and then calls sum_sum(column, res.local_pos() + 1) with NO check that
    // the delegated answer is a row of the buffer. sum_sum opens with
    // MEMORIA_ASSERT(idx <= size()) (:1601), which terminates the process.
    // The delegated search overruns exactly when its target sits on the final
    // prefix plateau — i.e. when the complement target total - val is not
    // exceeded by any prefix — which for the backward range [0, end) means
    // val == 0 with no weight after the range. The strict form is immune:
    // its target is strictly below prefix(end), so its answer is pinned
    // inside the range. This predicate is computed from the definition here,
    // not read out of the C++.
    auto terminates_head = [&](size_t c, size_t end, uint64_t v, bool strict) {
        if (strict) {
            return false;
        }
        int64_t total_end = range(c, 0, end);
        if ((int64_t)v > total_end) {
            return false;      // HEAD returns FindResult(start+1, total)
        }
        return naive_fw(c, (uint64_t)(total_end - (int64_t)v), true) >= rows;
    };

    // Start set: 0, the span edges and their neighbours, the midpoint, the
    // last row, and the end.
    std::vector<size_t> starts;
    for (size_t s: {(size_t)0, (size_t)1, (size_t)31, (size_t)32, (size_t)33,
                    rows / 2, rows ? rows - 1 : 0, rows}) {
        if (s <= rows) {
            starts.push_back(s);
        }
    }
    std::sort(starts.begin(), starts.end());
    starts.erase(std::unique(starts.begin(), starts.end()), starts.end());

    std::vector<std::array<uint64_t, 7>> qs;
    std::vector<std::array<uint64_t, 8>> divs;

    for (size_t c = 0; c < Columns; c++) {
        int64_t total = range(c, 0, rows);
        // 0 is always probed: it is the plateau value that carries #27.
        std::vector<uint64_t> probes{0, 1, (uint64_t)total, (uint64_t)(total + 1)};
        if (total > 0) {
            probes.push_back((uint64_t)(total - 1));
        }
        for (size_t k: starts) {
            int64_t p = range(c, 0, k);
            probes.push_back((uint64_t)p);
            probes.push_back((uint64_t)(p + 1));
            if (p > 0) {
                probes.push_back((uint64_t)(p - 1));
            }
        }
        std::sort(probes.begin(), probes.end());
        probes.erase(std::unique(probes.begin(), probes.end()), probes.end());

        for (int dir = 0; dir < 3; dir++) {
            for (size_t st: starts) {
                if (dir == 1 && st >= rows) {
                    continue;   // backward-from needs a real row
                }
                if (dir == 2 && st != starts[0]) {
                    continue;   // the full form takes no start
                }
                for (uint64_t v: probes) {
                    for (int op = 0; op < 2; op++) {
                        bool strict = (op == 1);
                        std::array<uint64_t, 2> want;
                        if (dir == 0) {
                            want = naive_from(c, st, v, strict);
                        } else if (dir == 1) {
                            want = naive_bw(c, st + 1, v, strict);
                        } else {
                            want = naive_bw(c, rows, v, strict);
                        }
                        uint64_t tok_start = (dir == 2) ? 0 : (uint64_t)st;

                        if (dir != 0) {
                            size_t end = (dir == 1) ? st + 1 : rows;
                            if (terminates_head(c, end, v, strict)) {
                                divs.push_back({(uint64_t)dir, (uint64_t)op,
                                                (uint64_t)c, tok_start, v, 1u, 0u, 0u});
                                continue;
                            }
                        }

                        uint64_t idx = 0;
                        uint64_t pfx = 0;
                        if (dir == 0) {
                            auto r = strict ? so.findGTForward(c, st, (int64_t)v)
                                            : so.findGEForward(c, st, (int64_t)v);
                            idx = (uint64_t)r.local_pos();
                            pfx = (uint64_t)(int64_t)r.prefix();
                        } else if (dir == 1) {
                            auto r = strict ? so.findGTBackward(c, st, (int64_t)v)
                                            : so.findGEBackward(c, st, (int64_t)v);
                            idx = (uint64_t)r.local_pos();
                            pfx = (uint64_t)(int64_t)r.prefix();
                        } else {
                            auto r = strict ? so.findGTBackward(c, (int64_t)v)
                                            : so.findGEBackward(c, (int64_t)v);
                            idx = (uint64_t)r.local_pos();
                            pfx = (uint64_t)(int64_t)r.prefix();
                        }

                        if (idx == want[0] && pfx == want[1]) {
                            qs.push_back({(uint64_t)dir, (uint64_t)op, (uint64_t)c,
                                          tok_start, v, idx, pfx});
                        } else {
                            divs.push_back({(uint64_t)dir, (uint64_t)op, (uint64_t)c,
                                            tok_start, v, 0u, idx, pfx});
                        }
                    }
                }
            }
        }
    }

    tok(3);
    tok(Columns);
    tok(rows);
    tok(seed);
    tok((uint64_t)mode);
    std::printf("\n");
    tok(qs.size());
    std::printf("\n");
    for (auto& q: qs) {
        for (uint64_t t: q) {
            tok(t);
        }
        std::printf("\n");
    }
    tok(divs.size());
    std::printf("\n");
    for (auto& d: divs) {
        for (uint64_t t: d) {
            tok(t);
        }
        std::printf("\n");
    }

    if (!divs.empty()) {
        size_t by[3][2] = {};
        for (auto& d: divs) {
            by[d[0]][d[5]]++;
        }
        std::fprintf(stderr,
            "DIVERGENCE dir-case C=%zu rows=%zu mode=%d: %zu of %zu queries "
            "disagree with the definition (upstream bug candidate #27) "
            "[fw-from wrong=%zu | bw-from wrong=%zu term=%zu"
            " | bw-full wrong=%zu term=%zu]\n",
            Columns, rows, mode, divs.size(), divs.size() + qs.size(),
            by[0][0], by[1][0], by[1][1], by[2][0], by[2][1]);
    }

    g_cases++;
}

// ── marker 4: MUTATION TRACES (rung P3) ─────────────────────────────────────
//
// Scripted op sequences run against HEAD's OWN two-phase surface
// (prepare_*/commit_*, so.hpp:533-1060) plus split_to/merge_with, dumping the
// post-state of every step so the Logos port can replay the identical script
// and compare.
//
// Two self-checks per step, and they are the interesting part:
//   VALUES  — HEAD's post-state against a naive std::vector model of the same
//             operation, written from its definition.
//   INDEX   — HEAD's index cells, level by level, against span sums
//             RECOMPUTED from HEAD's own post-state rows. This is the check
//             that catches a mutation which forgets to reindex: the values
//             can be perfectly right while every cached span sum is stale.
// Both flags are emitted per step; the Logos gate requires values parity
// unconditionally and index parity wherever HEAD reports itself coherent.
//
//   4 <columns> <rows> <seed>
//   <n_steps>
//     <op> <a> <b>                     op 0=insert 1=update 2=remove
//                                         3=split+merge-back
//     <n_in> <in values ...>           row-major inputs the step consumed
//     <post_rows> <values_ok> <index_ok>
//     <n_probe> (<row> <col> <val>) x n_probe
//     <n_levels> (<level_rows> <cells ...>) x n_levels
//
// Every row is probed at or below 128 rows; above that every 31st, so a
// 1025-row case stays a few KB while still covering every span.
const size_t MUT_ROW_SET[] = {5, 33, 65, 100, 1025};

template <size_t Columns>
void dump_mutation_case(size_t rows, uint64_t seed)
{
    using Buf = PackedDataTypeBufferT<BigInt, true, Columns, DTOrdering::SUM>;

    Lcg lcg(seed);
    std::vector<std::vector<int64_t>> model(Columns);
    for (size_t c = 0; c < Columns; c++) {
        for (size_t i = 0; i < rows; i++) {
            model[c].push_back((int64_t)(lcg.next() % 23));
        }
    }

    auto holder = PkdStructHolder<Buf>::make_empty(
        Buf::compute_block_size(rows + 256) + 256 * 1024);
    auto so = holder->get_so();
    so.insert_from_fn(0, rows, [&](size_t column, size_t row) {
        return model[column][row];
    });

    // The script: deterministic in (rows, seed), and shaped to cross the span
    // boundary in both directions rather than to stay on one side of it.
    struct Step { int op; size_t a; size_t b; };
    std::vector<Step> script = {
        {0, 0,           7},                       // insert at the front
        {0, rows / 2,    33},                      // insert straddling a span
        {1, rows / 3,    5},                       // update inside one span
        {1, 0,           40},                      // update across spans
        {2, 1,           9},                       // remove a small range
        {0, 0,           1},                       // insert one at the front
        {2, 0,           3},                       // remove from the front
        {3, rows / 2,    0},                       // split off the suffix
        {0, 0,           4},                       // insert into the remainder
    };

    tok(4);
    tok(Columns);
    tok(rows);
    tok(seed);
    std::printf("\n");
    tok(script.size());
    std::printf("\n");

    for (auto& st: script) {
        std::vector<int64_t> inputs;
        size_t cur = so.size();

        if (st.op == 0) {
            size_t at = std::min(st.a, cur);
            size_t n  = st.b;
            for (size_t r = 0; r < n; r++) {
                for (size_t c = 0; c < Columns; c++) {
                    inputs.push_back((int64_t)(lcg.next() % 23));
                }
            }
            auto us = so.make_update_state();
            auto status = so.prepare_insert(at, n, us.first,
                [&](size_t column, size_t row) {
                    return inputs[row * Columns + column];
                });
            if (!is_success(status)) {
                std::fprintf(stderr, "SELFCHECK mut prepare_insert refused\n");
                g_failed_selfchecks++;
            }
            so.commit_insert(at, n, us.first, [&](size_t column, size_t row) {
                return inputs[row * Columns + column];
            });
            for (size_t c = 0; c < Columns; c++) {
                model[c].insert(model[c].begin() + at, n, 0);
                for (size_t r = 0; r < n; r++) {
                    model[c][at + r] = inputs[r * Columns + c];
                }
            }
            tok(0); tok(at); tok(n);
        }
        else if (st.op == 1) {
            size_t at = std::min(st.a, cur);
            size_t n  = std::min(st.b, cur - at);
            for (size_t r = 0; r < n; r++) {
                for (size_t c = 0; c < Columns; c++) {
                    inputs.push_back((int64_t)(lcg.next() % 23));
                }
            }
            auto us = so.make_update_state();
            auto status = so.prepare_update(at, n, us.first,
                [&](size_t column, size_t row) {
                    return inputs[row * Columns + column];
                });
            if (!is_success(status)) {
                std::fprintf(stderr, "SELFCHECK mut prepare_update refused\n");
                g_failed_selfchecks++;
            }
            so.commit_update(at, n, us.first, [&](size_t column, size_t row) {
                return inputs[row * Columns + column];
            });
            for (size_t c = 0; c < Columns; c++) {
                for (size_t r = 0; r < n; r++) {
                    model[c][at + r] = inputs[r * Columns + c];
                }
            }
            tok(1); tok(at); tok(n);
        }
        else if (st.op == 2) {
            size_t s = std::min(st.a, cur);
            size_t e = std::min(s + st.b, cur);
            auto us = so.make_update_state();
            so.prepare_remove(s, e, us.first);
            so.commit_remove(s, e, us.first);
            for (size_t c = 0; c < Columns; c++) {
                model[c].erase(model[c].begin() + s, model[c].begin() + e);
            }
            tok(2); tok(s); tok(e - s);
        }
        else {
            // SPLIT: the suffix [k, size) leaves for a fresh buffer and the
            // SUBJECT keeps the prefix, which is what the trace then follows.
            //
            // Note what HEAD's split_to (:533) is, because the port
            // deliberately is not it: no prepare, no budget, `other` grown
            // and filled and only then `commit_remove` on self — an OOM
            // between the two leaves both nodes torn, which is precisely the
            // state the copy-node-to-scratch machinery exists to undo.
            size_t k = std::min(st.a, cur);
            auto oh = PkdStructHolder<Buf>::make_empty(
                Buf::compute_block_size(cur + 64) + 256 * 1024);
            auto other = oh->get_so();
            so.split_to(other, k);
            for (size_t c = 0; c < Columns; c++) {
                model[c].erase(model[c].begin() + k, model[c].end());
            }
            tok(3); tok(k); tok(0);
        }
        std::printf("\n");

        tok(inputs.size());
        for (auto v: inputs) {
            tok((uint64_t)v);
        }
        std::printf("\n");

        // ── self-check 1: the rows ────────────────────────────────────────
        size_t post = so.size();
        bool values_ok = (post == model[0].size());
        if (values_ok) {
            for (size_t c = 0; c < Columns && values_ok; c++) {
                for (size_t i = 0; i < post; i++) {
                    if ((int64_t)so.access(c, i) != model[c][i]) {
                        values_ok = false;
                        break;
                    }
                }
            }
        }

        // ── self-check 2: the index, recomputed from HEAD's OWN rows ──────
        std::vector<size_t> level_rows;
        std::vector<std::vector<uint64_t>> levels;
        bool index_ok = true;
        {
            auto cur_so = so;
            std::vector<std::vector<int64_t>> below(Columns);
            for (size_t c = 0; c < Columns; c++) {
                for (size_t i = 0; i < post; i++) {
                    below[c].push_back((int64_t)so.access(c, i));
                }
            }
            while (cur_so.data()->has_index()) {
                auto ix = cur_so.index();
                size_t n = ix.size();
                std::vector<uint64_t> cells;
                std::vector<std::vector<int64_t>> up(Columns);
                for (size_t r = 0; r < n; r++) {
                    for (size_t c = 0; c < Columns; c++) {
                        uint64_t got = (uint64_t)(int64_t)ix.access(c, r);
                        cells.push_back(got);
                        int64_t want = 0;
                        for (size_t i = r * 32;
                             i < std::min((r + 1) * 32, below[c].size()); i++) {
                            want += below[c][i];
                        }
                        if (got != (uint64_t)want) {
                            index_ok = false;
                        }
                        up[c].push_back(want);
                    }
                }
                // An index that does not cover every live row is itself an
                // incoherence, and one this family is known to produce.
                if (n * 32 < below[0].size()) {
                    index_ok = false;
                }
                level_rows.push_back(n);
                levels.push_back(cells);
                below = up;
                cur_so = ix;
            }
        }

        if (!values_ok) {
            std::fprintf(stderr,
                "DIVERGENCE mut C=%zu rows=%zu: HEAD post-state disagrees with "
                "the naive model after op %d\n", Columns, rows, st.op);
        }
        if (!index_ok) {
            std::fprintf(stderr,
                "DIVERGENCE mut C=%zu rows=%zu: HEAD index cells are STALE "
                "after op %d (recomputed from HEAD's own rows)\n",
                Columns, rows, st.op);
        }

        tok(post);
        tok(values_ok ? 1 : 0);
        tok(index_ok ? 1 : 0);
        std::printf("\n");

        size_t stride = post <= 128 ? 1 : 31;
        std::vector<std::array<uint64_t, 3>> probes;
        for (size_t i = 0; i < post; i += stride) {
            for (size_t c = 0; c < Columns; c++) {
                probes.push_back({(uint64_t)i, (uint64_t)c,
                                  (uint64_t)(int64_t)so.access(c, i)});
            }
        }
        tok(probes.size());
        for (auto& p: probes) {
            tok(p[0]); tok(p[1]); tok(p[2]);
        }
        std::printf("\n");

        tok(levels.size());
        std::printf("\n");
        for (size_t l = 0; l < levels.size(); l++) {
            tok(level_rows[l]);
            for (auto v: levels[l]) {
                tok(v);
            }
            std::printf("\n");
        }
    }

    g_cases++;
}


// ── marker 5: THE VARIABLE-LENGTH DIMENSION (rung P4) ───────────────────────
//
// `PackedDataTypeBufferT<Varchar, false, C, DTOrdering::UNORDERED>` — the VLE
// half of the family (PDTDimension<Span<T0>,…>, vle_tools.hpp), Width 2:
// a payload block plus a psize_t offsets table.
//
// MARKER 5 (behavioural, trustworthy) is the ONLY marker this rung emits. It
// carries the final state of a scripted insert / SINGLE-ROW update / remove
// sequence, self-checked at every step against a naive
// vector<vector<string>> model and emitted only where HEAD agreed with it —
// so these fixtures pin the variable-length paths where HEAD is RIGHT.
//
//   5 <columns> <rows>
//     <len> <bytes ...>            x rows*columns, ROW-MAJOR
//
// There is deliberately NO marker 6. Multi-row updates are upstream defect
// #28, and emitting HEAD's answer for them would mean reading back a block
// HEAD has already corrupted; see the full write-up under "WHY THERE IS NO
// MARKER 6" below, at the end of this section. The divergence is pinned on the
// Logos side instead — tests/pdtbuf_core.logos section 1340.

template <size_t Columns>
using VleBufSO = typename PackedDataTypeBufferT<Varchar, false, Columns,
                                                DTOrdering::UNORDERED>::SparseObject;

template <size_t Columns>
struct VleModel {
    std::vector<std::vector<std::string>> col{Columns};
};

static std::string vle_str(Lcg& lcg, size_t maxlen)
{
    size_t n = lcg.next() % (maxlen + 1);
    std::string s;
    s.reserve(n);
    for (size_t i = 0; i < n; i++) {
        s.push_back((char)('a' + lcg.next() % 26));
    }
    return s;
}

static void tok_span(const std::string& s)
{
    tok(s.size());
    for (size_t i = 0; i < s.size(); i++) {
        tok((uint8_t)s[i]);
    }
}

template <size_t Columns>
static bool vle_agrees(const VleBufSO<Columns>& so, const VleModel<Columns>& m,
                       const char* what, size_t step)
{
    if ((size_t)so.size() != m.col[0].size()) {
        std::fprintf(stderr, "SELFCHECK vle[%s step %zu] C=%zu size %zu vs %zu\n",
                     what, step, Columns, (size_t)so.size(), m.col[0].size());
        return false;
    }
    for (size_t c = 0; c < Columns; c++) {
        for (size_t r = 0; r < m.col[c].size(); r++) {
            auto v = so.access(c, r);
            std::string got((const char*)v.data(), v.size());
            if (got != m.col[c][r]) {
                std::fprintf(stderr,
                    "SELFCHECK vle[%s step %zu] C=%zu col %zu row %zu '%s' vs '%s'\n",
                    what, step, Columns, c, r, got.c_str(), m.col[c][r].c_str());
                return false;
            }
        }
    }
    return true;
}

template <size_t Columns>
void dump_vle_case(size_t rows, uint64_t seed, size_t maxlen)
{
    using Buf = PackedDataTypeBufferT<Varchar, false, Columns, DTOrdering::UNORDERED>;
    Lcg lcg(seed);

    VleModel<Columns> m;
    for (size_t c = 0; c < Columns; c++) {
        for (size_t r = 0; r < rows; r++) {
            m.col[c].push_back(vle_str(lcg, maxlen));
        }
    }

    auto holder = PkdStructHolder<Buf>::make_empty(512 * 1024);
    auto so = holder->get_so();
    if (rows > 0) {
        so.insert_from_fn(0, rows, [&](size_t column, size_t row) {
            return U8StringView(m.col[column][row].data(), m.col[column][row].size());
        });
    }
    if (!vle_agrees<Columns>(so, m, "seed", 0)) {
        g_failed_selfchecks++;
        return;
    }

    // A short scripted sequence over the paths HEAD gets right: an insert in
    // the middle, a single-row update, and a removal.
    if (rows > 2) {
        size_t at = rows / 2;
        std::vector<std::vector<std::string>> ins(Columns);
        for (size_t c = 0; c < Columns; c++) {
            ins[c].push_back(vle_str(lcg, maxlen + 3));
            ins[c].push_back(std::string());          // an EMPTY span, on purpose
        }
        auto acc = [&](size_t column, size_t row) {
            return U8StringView(ins[column][row].data(), ins[column][row].size());
        };
        auto us = so.make_update_state();
        if (is_success(so.prepare_insert(at, 2, us.first, acc))) {
            so.commit_insert(at, 2, us.first, acc);
            for (size_t c = 0; c < Columns; c++) {
                m.col[c].insert(m.col[c].begin() + at, ins[c].begin(), ins[c].end());
            }
        }
        if (!vle_agrees<Columns>(so, m, "insert", 1)) {
            g_failed_selfchecks++;
            return;
        }

        std::vector<std::string> upd(Columns);
        for (size_t c = 0; c < Columns; c++) {
            upd[c] = vle_str(lcg, maxlen + 5);
        }
        auto uacc = [&](size_t column, size_t) {
            return U8StringView(upd[column].data(), upd[column].size());
        };
        auto us2 = so.make_update_state();
        if (is_success(so.prepare_update(1, 1, us2.first, uacc))) {
            so.commit_update(1, 1, us2.first, uacc);
            for (size_t c = 0; c < Columns; c++) {
                m.col[c][1] = upd[c];
            }
        }
        if (!vle_agrees<Columns>(so, m, "update1", 2)) {
            g_failed_selfchecks++;
            return;
        }

        auto us3 = so.make_update_state();
        if (is_success(so.prepare_remove(0, 2, us3.first))) {
            so.commit_remove(0, 2, us3.first);
            for (size_t c = 0; c < Columns; c++) {
                m.col[c].erase(m.col[c].begin(), m.col[c].begin() + 2);
            }
        }
        if (!vle_agrees<Columns>(so, m, "remove", 3)) {
            g_failed_selfchecks++;
            return;
        }
    }

    tok(5); tok(Columns); tok(m.col[0].size());
    std::printf("\n");
    for (size_t r = 0; r < m.col[0].size(); r++) {
        for (size_t c = 0; c < Columns; c++) {
            tok_span(m.col[c][r]);
        }
    }
    std::printf("\n");
    g_cases++;
}

// ── WHY THERE IS NO MARKER 6 (upstream defect #28) ─────────────────────────
//
// A multi-row update over a VLE dimension is not merely WRONG in HEAD, it is
// MEMORY-UNSAFE, and that is why this file records the defect in prose and in a
// Logos-side probe instead of in a fixture.
//
//   do_commit_update_var_max (so.hpp:1050-1084) corrects only the AGGREGATE
//   length of [row_at, row_at + size) — via resize_block(row_at, size, Sum of
//   the new lengths) — and then writes each row through replace_row
//   (vle_tools.hpp:320-325), which memcpy's value.length() bytes at
//   offsets[idx] and NEVER assigns offsets[idx + 1]. The interior boundaries of
//   the range therefore keep their OLD positions, so every updated row but the
//   first reads back truncated or padded, and each write runs for its NEW
//   length from a STALE start, overrunning whatever follows.
//
//   Minimal, with no length change at all (verified against HEAD):
//       corpus  ["aaa", "bbbb", "ccccc", "dd", "e"], update rows [0,2) with
//               ["bbbb", "aaa"]
//       HEAD -> ["bbb", "aaab", "ccccc", "dd", "e"]   (boundary still at 3)
//       true -> ["bbbb", "aaa",  "ccccc", "dd", "e"]
//   Single-row updates are CORRECT (resize_block does fix offsets[row_at + 1]),
//   which is what isolates the defect: a randomized sequence of insert /
//   remove / split / merge / single-row-update — 24 sequences x 60 steps,
//   columns 1..4 — passes with ZERO self-check failures, while allowing
//   multi-row updates makes it diverge on the first one and SIGSEGV soon after.
//   Reachable through the public commit_update on the only dispatcher arm a
//   VARIABLE datatype has (:249; SUM/VARIABLE is excluded by :349).
//
// Emitting HEAD's answer as a fixture would mean reading a block HEAD has
// already corrupted, so the divergence is pinned on the Logos side instead:
// tests/pdtbuf_core.logos carries a probe that transcribes this C++ SHAPE —
// correct the aggregate, leave the interior boundaries alone — and it must
// FAIL the gate. An upstream fix will make that probe stop being a divergence,
// and the prose here is what says so.

template <size_t Columns>
void dump_columns()
{
    uint64_t seed = 0x9E3779B97F4A7C15ull + Columns * 1013904223ull;
    for (size_t rows: ROW_SET) {
        dump_case<Columns, DTOrdering::SUM>(rows, seed);
        seed += 7919;
        dump_case<Columns, DTOrdering::MAX>(rows, seed);
        seed += 7919;
    }
    for (size_t rows: DEEP_ROW_SET) {
        dump_index_case<Columns>(rows, seed);
        seed += 7919;
    }
    for (size_t rows: DIR_ROW_SET) {
        dump_dir_case<Columns>(rows, seed, 0);
        seed += 7919;
        dump_dir_case<Columns>(rows, seed, 1);
        seed += 7919;
    }
    for (size_t rows: MUT_ROW_SET) {
        dump_mutation_case<Columns>(rows, seed);
        seed += 7919;
    }
    // P4: the variable-length dimension. Row counts include the 0/1 corners,
    // and maxlen 0 makes an ENTIRE corpus of empty spans.
    for (size_t rows: VLE_ROW_SET) {
        dump_vle_case<Columns>(rows, seed, 7);
        seed += 7919;
        dump_vle_case<Columns>(rows, seed, 0);
        seed += 7919;
    }
}

}  // namespace

int main()
{
    dump_columns<1>();
    dump_columns<2>();
    dump_columns<3>();
    dump_columns<4>();

    tok(0);
    std::printf("\n");

    std::fprintf(stderr, "pdtbuf_dump: %zu cases, %zu failed self-checks\n",
                 g_cases, g_failed_selfchecks);
    return g_failed_selfchecks == 0 ? 0 : 1;
}
