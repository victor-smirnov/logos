fn both<'a>(t: (*const &'a i64, fn(&'a i64) -> i64), x: &'a i64) -> i64 {
    let g: fn(&'a i64) -> i64 = t.1; g(x) + unsafe { **t.0 }
}
fn rd(x: &i64) -> i64 { *x }
fn logos_main() -> i32 { let v: i64 = 4; let r: &i64 = &v; let p: *const &i64 = &r; (both((p, rd), &v) - 8) as i32 }
fn main() { std::process::exit(logos_main()); }
