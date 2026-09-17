fn deep<'a>(q: *const *const &'a i64) -> &'a i64 { unsafe { **q } }
fn logos_main() -> i32 {
    let out: &i64;
    { let v: i64 = 7; let r: &i64 = &v; let p: *const &i64 = &r; let qq: *const *const &i64 = &p; out = deep(qq); }
    *out as i32
}
fn main() { std::process::exit(logos_main()); }
