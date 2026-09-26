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

// ── Rust's float rendering (Display / Debug / `{:.N}`) ─────────────────────
// Display: the SHORTEST decimal that parses back to the same value, written
// in positional notation (never an exponent); `-0`, `inf`, `-inf`, `NaN`.
// Debug: the same digits, scientific when the decimal exponent is >= 16 or
// < -4 (`1e16`, `1.5e-7`), otherwise positional with `.0` on an integral value.
// Precision: positional with exactly N fraction digits, exact round-half-even
// (snprintf's `%.*f` on the binary value) — `{:.0}` of 2.5 is "2".
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int logos_put(char* buf, int32_t cap, int n, const char* s) {
    int len = (int)strlen(s);
    for (int i = 0; i < len && n < cap - 1; ++i) buf[n++] = s[i];
    return n;
}

// digits (no dot) + decimal exponent of the leading digit, shortest round-trip.
static void logos_shortest(double x, int is_f32, char* digits, int* exp10) {
    char tmp[64];
    int maxp = is_f32 ? 9 : 17;
    for (int p = 1; p <= maxp; ++p) {
        snprintf(tmp, sizeof tmp, "%.*e", p - 1, x);
        int ok = is_f32 ? (strtof(tmp, NULL) == (float)x) : (strtod(tmp, NULL) == x);
        if (ok || p == maxp) break;
    }
    // tmp = d[.ddd]e±XX
    int k = 0;
    const char* c = tmp;
    for (; *c && *c != 'e'; ++c) if (*c >= '0' && *c <= '9') digits[k++] = *c;
    while (k > 1 && digits[k - 1] == '0') --k;   // `%.*e` pads with zeros only at the shortest p
    digits[k] = 0;
    *exp10 = (*c == 'e') ? atoi(c + 1) : 0;
}

static int32_t logos_fmt_float(char* buf, int32_t cap, double x, int is_f32, int debug) {
    int n = 0;
    if (isnan(x)) { n = logos_put(buf, cap, n, "NaN"); buf[n] = 0; return n; }
    if (signbit(x)) { n = logos_put(buf, cap, n, "-"); x = -x; }
    if (isinf(x)) { n = logos_put(buf, cap, n, "inf"); buf[n] = 0; return n; }
    if (x == 0.0) { n = logos_put(buf, cap, n, debug ? "0.0" : "0"); buf[n] = 0; return n; }
    char d[32]; int e;
    logos_shortest(x, is_f32, d, &e);
    int nd = (int)strlen(d);
    if (debug && (e >= 16 || e < -4)) {
        if (n < cap - 1) buf[n++] = d[0];
        if (nd > 1) { if (n < cap - 1) buf[n++] = '.'; for (int i = 1; i < nd && n < cap - 1; ++i) buf[n++] = d[i]; }
        char ex[16]; snprintf(ex, sizeof ex, "e%d", e);
        n = logos_put(buf, cap, n, ex);
        buf[n] = 0; return n;
    }
    if (e >= 0) {
        for (int i = 0; i <= e && n < cap - 1; ++i) buf[n++] = i < nd ? d[i] : '0';
        if (nd > e + 1) {
            if (n < cap - 1) buf[n++] = '.';
            for (int i = e + 1; i < nd && n < cap - 1; ++i) buf[n++] = d[i];
        } else if (debug) n = logos_put(buf, cap, n, ".0");
    } else {
        n = logos_put(buf, cap, n, "0.");
        for (int i = 0; i < -e - 1 && n < cap - 1; ++i) buf[n++] = '0';
        for (int i = 0; i < nd && n < cap - 1; ++i) buf[n++] = d[i];
    }
    buf[n] = 0;
    return n;
}

int32_t logos_fmt_f64_rust(char* buf, int32_t cap, double x, int32_t debug) {
    return logos_fmt_float(buf, cap, x, 0, debug);
}
int32_t logos_fmt_f32_rust(char* buf, int32_t cap, float x, int32_t debug) {
    return logos_fmt_float(buf, cap, (double)x, 1, debug);
}

int32_t logos_fmt_f64_fixed(char* buf, int32_t cap, double x, int32_t prec) {
    int n;
    if (isnan(x)) n = snprintf(buf, (size_t)cap, "NaN");
    else if (isinf(x)) n = snprintf(buf, (size_t)cap, signbit(x) ? "-inf" : "inf");
    else n = snprintf(buf, (size_t)cap, "%.*f", (int)prec, x);
    if (n < 0) return 0;
    return (n >= cap) ? cap - 1 : n;
}
