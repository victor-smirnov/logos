fn both<'a>(t: (*const &'a i64, fn(&'a i64) -> &'a i64), x: &'a i64) -> &'a i64 { let g: fn(&'a i64) -> &'a i64 = t.1; g(x) }
fn id(x: &i64) -> &i64 { x }
fn logos_main() -> i32 {
    let out: &i64;
    { let v: i64 = 5; let r: &i64 = &v; let p: *const &i64 = &r; out = both((p, id), &v); }
    *out as i32
}
fn main() { std::process::exit(logos_main()); }
