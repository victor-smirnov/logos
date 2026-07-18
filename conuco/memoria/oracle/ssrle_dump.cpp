// ssrle_dump — C++ SSRLE codec oracle for the Logos port (increment 1).
//
// Includes the REAL Memoria codec headers (~/cxx/memoria) and dumps, for a
// deterministic corpus over Bps 1..8, the run triples and the exact u16 unit
// streams produced by SSRLERunTraits<Bps>::write_segments_to. The output is
// committed as tests/ssrle_fixtures.hex and consumed by tests/ssrle_codec.logos,
// which must reproduce every unit stream BYTE-IDENTICALLY.
//
// Build + regenerate (see oracle/README.md for the include-path notes):
//
//   g++ -std=c++20 -O1 -DMMA_ICU_CXX_NS=icu_74 \
//       -I $HOME/cxx/memoria/core/include \
//       -I $HOME/cxx/memoria/build/Clang_19-Debug/vcpkg_installed/x64-linux/include \
//       oracle/ssrle_dump.cpp -o /tmp/ssrle_dump \
//   && /tmp/ssrle_dump > tests/ssrle_fixtures.hex
//
// Output format (whitespace-separated lowercase hex tokens, no 0x):
//   <bps> <nruns> <nunits>
//   <plen> <pattern> <rl>     x nruns
//   <unit> ...                x nunits
//   ... repeated per case; terminated by a single 0 token.
//
// No time, no non-seeded randomness — the corpus is a pure function of this
// file. Every case is self-checked here: compute_size == write_segments_to
// result, and an as_vector-semantics decode roundtrips to the source runs.

#include <memoria/core/ssrle/ssrle.hpp>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

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
    uint64_t next64() {
        uint64_t hi = next();
        uint64_t lo = next();
        return (hi << 32) | (lo & 0xFFFFFFFFull);
    }
};

template <size_t Bps>
struct CaseSink {
    using Traits = SSRLERunTraits<Bps>;
    using RunT   = SSRLERun<Bps>;
    using UnitT  = typename Traits::CodeUnitT;

    size_t emitted = 0;

    // As-vector decode semantics (ssrleseq_iterator.hpp:81): collect data
    // runs, skip padding by its run_length, stop at a null run.
    static std::vector<RunT> decode_stream(const std::vector<UnitT>& units, size_t limit) {
        std::vector<RunT> out;
        size_t idx = 0;
        while (idx < limit) {
            RunT run;
            size_t sz = Traits::decode_unit_to(units.data() + idx, run);
            if (run) {
                out.push_back(run);
                idx += sz;
            }
            else if (run.is_padding()) {
                idx += run.run_length();
            }
            else {
                break;
            }
        }
        return out;
    }

    void emit(const std::vector<RunT>& runs) {
        size_t size = Traits::compute_size(Span<const RunT>(runs.data(), runs.size()), 0);
        std::vector<UnitT> units(size, UnitT{0});
        size_t written = Traits::write_segments_to(
            Span<const RunT>(runs.data(), runs.size()),
            Span<UnitT>(units.data(), units.size()),
            0);
        if (written != size) {
            std::fprintf(stderr, "FATAL bps=%zu: compute_size %zu != written %zu\n",
                         Bps, size, written);
            std::exit(1);
        }

        std::vector<RunT> decoded = decode_stream(units, written);
        if (decoded.size() != runs.size()) {
            std::fprintf(stderr, "FATAL bps=%zu: roundtrip count %zu != %zu\n",
                         Bps, decoded.size(), runs.size());
            std::exit(1);
        }
        for (size_t i = 0; i < runs.size(); i++) {
            if (!(decoded[i] == runs[i])) {
                std::fprintf(stderr, "FATAL bps=%zu: roundtrip mismatch at run %zu\n", Bps, i);
                std::exit(1);
            }
        }

        std::printf("%zx %zx %zx\n", Bps, runs.size(), size);
        for (const RunT& r : runs) {
            std::printf("%llx %llx %llx\n",
                        (unsigned long long) r.pattern_length(),
                        (unsigned long long) r.pattern(),
                        (unsigned long long) r.run_length());
        }
        for (size_t i = 0; i < size; i++) {
            std::printf("%x%c", (unsigned) units[i], (i % 16 == 15 || i + 1 == size) ? '\n' : ' ');
        }
        emitted++;
    }
};

template <size_t Bps>
uint64_t alternating_pattern(size_t plen) {
    // symbols 0,1,0,1,... but for Bps>1 alternate min/max symbol values
    uint64_t max_sym = (Bps == 8) ? 0xFF : ((uint64_t{1} << Bps) - 1);
    uint64_t p = 0;
    for (size_t i = 0; i < plen; i++) {
        if (i % 2 == 1) {
            p |= (max_sym << (i * Bps));
        }
    }
    return p;
}

// Smallest run {plen=1, pattern=1&mask} with an exact unit footprint, found by
// scanning rl = 2^k - 1. Dies if the footprint is unreachable for this Bps.
template <size_t Bps>
SSRLERun<Bps> run_with_units(size_t target_units) {
    using Traits = SSRLERunTraits<Bps>;
    uint64_t max_rl = Traits::max_run_length(1);
    for (size_t k = 0; k < 62; k++) {
        uint64_t rl = (uint64_t{1} << k) - 1;
        if (rl < 1 || rl > max_rl) {
            continue;
        }
        SSRLERun<Bps> r(1, 1, rl);
        if (Traits::estimate_size(r) == target_units) {
            return r;
        }
    }
    std::fprintf(stderr, "FATAL bps=%zu: no run with %zu units\n", Bps, target_units);
    std::exit(1);
}

template <size_t Bps>
void run_corpus() {
    using Traits = SSRLERunTraits<Bps>;
    using RunT   = SSRLERun<Bps>;

    CaseSink<Bps> sink;

    uint64_t max_sym  = (Bps == 8) ? 0xFF : ((uint64_t{1} << Bps) - 1);
    uint64_t ones     = ~uint64_t{0};
    constexpr size_t LEN_BITS = Traits::LEN_BITS;

    // ENCODABLE plen cap. CODEC EDGE (found while building this corpus):
    // for Bps=7, max_pattern_length() = DATA_BITS/Bps = 59/7 = 8, but the
    // 3-bit LEN field can only hold plen <= 7 — a plen-8 run encodes
    // CORRUPTLY in C++ (plen wraps to 0 => null run). Every other Bps fits.
    // The corpus stays within the encodable range; the quirk is preserved,
    // not fixed, on both sides.
    size_t max_plen = Traits::max_pattern_length();
    size_t len_cap  = (size_t{1} << LEN_BITS) - 1;
    if (max_plen > len_cap) {
        max_plen = len_cap;
    }

    // ── single runs: rl=1 zero-bit cases; pattern content extremes ──
    sink.emit({RunT(1, 0, 1)});
    sink.emit({RunT(1, max_sym, 1)});
    sink.emit({RunT(max_plen, alternating_pattern<Bps>(max_plen), 1)});
    sink.emit({RunT(max_plen, ones, 1)});             // ctor masks to plen*Bps
    sink.emit({RunT(max_plen, 0, 1)});
    if (max_plen / 2 >= 1) {
        sink.emit({RunT(max_plen / 2, alternating_pattern<Bps>(max_plen / 2), 1)});
    }

    // ── single runs: rl at every residual-bit budget boundary ±1 ──
    size_t plens[3] = {1, 2, max_plen / 2};
    for (size_t pi = 0; pi < 3; pi++) {
        size_t plen = plens[pi];
        if (plen < 1 || plen > max_plen || (pi > 0 && plen == plens[pi - 1])) {
            continue;
        }
        uint64_t max_rl = Traits::max_run_length(plen);
        uint64_t pattern = alternating_pattern<Bps>(plen);
        for (size_t total_bits = 16; total_bits <= 64; total_bits += 16) {
            long rb = (long) total_bits - 2 - (long) LEN_BITS - (long) (plen * Bps);
            if (rb <= 0) {
                continue;
            }
            uint64_t base = uint64_t{1} << rb;
            uint64_t cands[4] = {base - 1, base, base + 1, base - 2};
            for (uint64_t rl : cands) {
                if (rl >= 1 && rl <= max_rl && Traits::is_fit(plen, rl)) {
                    sink.emit({RunT(plen, pattern, rl)});
                }
            }
        }
        // small run lengths + the plen-specific maximum
        sink.emit({RunT(plen, pattern, 2)});
        sink.emit({RunT(plen, pattern, 3)});
        sink.emit({RunT(plen, pattern, max_rl)});
    }

    // ── multi-run streams: padding at exact 32-unit edges ──
    {
        // 40 one-unit runs — crosses the boundary with the ==limit bump, no padding
        std::vector<RunT> s(40, RunT(1, 1, 1));
        sink.emit(s);
    }
    {
        // remainder 2: ten 3-unit runs (30 units), next forces padding + 1 null
        std::vector<RunT> s(15, run_with_units<Bps>(3));
        sink.emit(s);
    }
    {
        // remainder 1: 31 one-unit runs then a 3-unit run — padding, no null fill
        std::vector<RunT> s(31, RunT(1, 1, 1));
        s.push_back(run_with_units<Bps>(3));
        s.push_back(RunT(1, 1, 1));
        sink.emit(s);
    }
    {
        // remainder 3: 29 one-unit runs then a 4-unit run — padding + 2 nulls
        std::vector<RunT> s(29, RunT(1, 1, 1));
        s.push_back(run_with_units<Bps>(4));
        s.push_back(RunT(1, max_sym, 1));
        sink.emit(s);
    }
    {
        // exact segment fills: 2-unit runs, 16 per segment, 2.5 segments worth
        std::vector<RunT> s(40, run_with_units<Bps>(2));
        sink.emit(s);
    }
    {
        // exact segment fills: 4-unit runs, 8 per segment, 3 segments
        std::vector<RunT> s(24, run_with_units<Bps>(4));
        sink.emit(s);
    }

    // ── long stream: 300 runs cycling the unit footprints ──
    {
        std::vector<RunT> s;
        RunT shapes[4] = {
            RunT(1, 1, 1),
            run_with_units<Bps>(2),
            run_with_units<Bps>(3),
            run_with_units<Bps>(4),
        };
        for (size_t i = 0; i < 300; i++) {
            s.push_back(shapes[i % 4]);
        }
        sink.emit(s);
    }

    // ── seeded random streams: 2 x 150 runs ──
    for (size_t stream = 0; stream < 2; stream++) {
        Lcg lcg(0xC0FFEE00ull + Bps * 16 + stream);
        std::vector<RunT> s;
        for (size_t i = 0; i < 150; i++) {
            size_t plen = 1 + (size_t) (lcg.next() % max_plen);
            uint64_t max_rl = Traits::max_run_length(plen);
            uint64_t cap = max_rl < 10000 ? max_rl : 10000;
            uint64_t rl = 1 + lcg.next() % cap;
            uint64_t pattern = lcg.next64();
            RunT r(plen, pattern, rl);          // ctor masks the pattern
            if (!Traits::is_fit(r.pattern_length(), r.run_length())) {
                std::fprintf(stderr, "FATAL bps=%zu: random run does not fit\n", Bps);
                std::exit(1);
            }
            s.push_back(r);
        }
        sink.emit(s);
    }

    std::fprintf(stderr, "bps=%zu: %zu cases\n", Bps, sink.emitted);
}

}  // namespace

int main() {
    run_corpus<1>();
    run_corpus<2>();
    run_corpus<3>();
    run_corpus<4>();
    run_corpus<5>();
    run_corpus<6>();
    run_corpus<7>();
    run_corpus<8>();
    std::printf("0\n");
    return 0;
}
