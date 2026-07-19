// pdtbuf_dump — C++ PackedDataTypeBuffer cross-check reference for the Logos
// PdtBuf port (ladder rung P0).
//
// ROLE (read this before trusting a byte of the output): this harness is a
// DIVERGENCE DETECTOR, not an authority. The C++ PDTBuffer/FQTree family is
// dense with bugs (23 catalogued during recon; two of them are exercised
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

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>
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
