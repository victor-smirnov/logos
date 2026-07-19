// logos_pdep_u64 / logos_pext_u64 — BMI2 parallel bit deposit/extract with
// runtime cpuid dispatch.
//
// The compiler lowers the `pdep_u64` / `pext_u64` intrinsics to the inline
// hardware instruction ONLY when the compile-time TARGET cpu has BMI2
// (`-C target-cpu=native` etc.). The default "generic" target calls these
// dispatchers instead: BMI2 presence is checked once via cpuid (cached), and
// capable hosts get the hardware instruction behind one predictable branch —
// glibc-ifunc-style benefit without ifunc machinery. Non-BMI2 hosts (and
// non-x86 targets) take the portable bit-loop.
//
// The *_soft variants are exported (not static) so tests can pin the portable
// path against the dispatched one — the fallback must never be dead untested
// code.

#include <stdint.h>

uint64_t logos_pdep_u64_soft(uint64_t x, uint64_t mask)
{
    uint64_t res = 0;
    uint64_t bit = 1;
    while (mask) {
        uint64_t low = mask & (uint64_t)(-(int64_t)mask); /* lowest set bit */
        if (x & bit)
            res |= low;
        mask &= mask - 1;
        bit <<= 1;
    }
    return res;
}

uint64_t logos_pext_u64_soft(uint64_t x, uint64_t mask)
{
    uint64_t res = 0;
    uint64_t bit = 1;
    while (mask) {
        uint64_t low = mask & (uint64_t)(-(int64_t)mask);
        if (x & low)
            res |= bit;
        mask &= mask - 1;
        bit <<= 1;
    }
    return res;
}

#if defined(__x86_64__)

#include <cpuid.h>

__attribute__((target("bmi2")))
static uint64_t pdep_hw(uint64_t x, uint64_t mask)
{
    return __builtin_ia32_pdep_di(x, mask);
}

__attribute__((target("bmi2")))
static uint64_t pext_hw(uint64_t x, uint64_t mask)
{
    return __builtin_ia32_pext_di(x, mask);
}

static int bitops_has_bmi2(void)
{
    static int cached = -1;
    if (cached < 0) {
        unsigned a = 0, b = 0, c = 0, d = 0;
        cached = 0;
        if (__get_cpuid_count(7, 0, &a, &b, &c, &d))
            cached = (int)((b >> 8) & 1u); /* EBX bit 8 = BMI2 */
    }
    return cached;
}

uint64_t logos_pdep_u64(uint64_t x, uint64_t mask)
{
    if (bitops_has_bmi2())
        return pdep_hw(x, mask);
    return logos_pdep_u64_soft(x, mask);
}

uint64_t logos_pext_u64(uint64_t x, uint64_t mask)
{
    if (bitops_has_bmi2())
        return pext_hw(x, mask);
    return logos_pext_u64_soft(x, mask);
}

#else /* non-x86 targets: portable path only */

uint64_t logos_pdep_u64(uint64_t x, uint64_t mask)
{
    return logos_pdep_u64_soft(x, mask);
}

uint64_t logos_pext_u64(uint64_t x, uint64_t mask)
{
    return logos_pext_u64_soft(x, mask);
}

#endif
