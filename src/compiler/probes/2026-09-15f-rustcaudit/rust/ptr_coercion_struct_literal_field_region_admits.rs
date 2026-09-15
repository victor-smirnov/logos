struct HS { p: *const &'static i64 }
struct TS(*const &'static i64);
fn f<'a>(x: & &'a i64) -> i64 {
    let h: HS = HS { p: x };
    let t: TS = TS(x);
    return 0i64;
}
fn main() {}
