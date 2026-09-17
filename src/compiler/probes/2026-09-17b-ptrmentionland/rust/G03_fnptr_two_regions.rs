// twin of hand/G03_fnptr_two_regions.logos — LEGAL
fn apply<'a, 'b>(g: fn(&'a i64, &'b i64) -> i64, x: &'a i64, y: &'b i64) -> i64 { g(x, y) }
fn add(x: &i64, y: &i64) -> i64 { *x + *y }
fn main() {
    let m: i64 = 4;
    let n: i64 = 6;
    std::process::exit((apply(add, &m, &n) - 10) as i32);
}
