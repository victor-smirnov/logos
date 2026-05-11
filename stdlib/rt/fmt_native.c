// SPDX-License-Identifier: Apache-2.0
// Native float-formatting helpers for std.fmt's Display / Debug /
// LowerExp / UpperExp impls on f32 and f64. snprintf does the heavy
// lifting; the Logos side just copies the resulting bytes into its
// String buffer.

#include <stdio.h>
#include <stdint.h>

// Default Display: "%g" — shortest round-trippable representation,
// drops trailing zeros and decimal point when not needed.
int32_t logos_fmt_f64_g(char* buf, int32_t cap, double x) {
    int n = snprintf(buf, (size_t)cap, "%g", x);
    if (n < 0) return 0;
    return (n >= cap) ? cap - 1 : n;
}

int32_t logos_fmt_f32_g(char* buf, int32_t cap, float x) {
    int n = snprintf(buf, (size_t)cap, "%g", (double)x);
    if (n < 0) return 0;
    return (n >= cap) ? cap - 1 : n;
}

// Debug: "%.17g" — full f64 round-trip precision so debug output
// round-trips through parse(). f32 uses "%.9g" (full f32 precision).
int32_t logos_fmt_f64_dbg(char* buf, int32_t cap, double x) {
    int n = snprintf(buf, (size_t)cap, "%.17g", x);
    if (n < 0) return 0;
    return (n >= cap) ? cap - 1 : n;
}

int32_t logos_fmt_f32_dbg(char* buf, int32_t cap, float x) {
    int n = snprintf(buf, (size_t)cap, "%.9g", (double)x);
    if (n < 0) return 0;
    return (n >= cap) ? cap - 1 : n;
}

// LowerExp / UpperExp: scientific notation.
int32_t logos_fmt_f64_e(char* buf, int32_t cap, double x) {
    int n = snprintf(buf, (size_t)cap, "%e", x);
    if (n < 0) return 0;
    return (n >= cap) ? cap - 1 : n;
}

int32_t logos_fmt_f64_E(char* buf, int32_t cap, double x) {
    int n = snprintf(buf, (size_t)cap, "%E", x);
    if (n < 0) return 0;
    return (n >= cap) ? cap - 1 : n;
}

int32_t logos_fmt_f32_e(char* buf, int32_t cap, float x) {
    int n = snprintf(buf, (size_t)cap, "%e", (double)x);
    if (n < 0) return 0;
    return (n >= cap) ? cap - 1 : n;
}

int32_t logos_fmt_f32_E(char* buf, int32_t cap, float x) {
    int n = snprintf(buf, (size_t)cap, "%E", (double)x);
    if (n < 0) return 0;
    return (n >= cap) ? cap - 1 : n;
}
