// twin of hand/G04_fnptr_region_in_return_only.logos — LEGAL
fn pick<'a>(g: fn(i64) -> &'a i64, k: i64) -> i64 { *g(k) }
static GLOB: i64 = 11;
fn getg(_k: i64) -> &'static i64 { &GLOB }
fn main() { std::process::exit((pick(getg, 1) - 11) as i32); }
