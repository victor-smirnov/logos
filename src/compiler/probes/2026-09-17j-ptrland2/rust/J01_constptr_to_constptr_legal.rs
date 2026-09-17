fn deep<'a>(q: *const *const &'a i64) -> i64 { unsafe { ***q } }
fn logos_main() -> i32 {
    let v: i64 = 7; let r: &i64 = &v;
    let p: *const &i64 = &r; let qq: *const *const &i64 = &p;
    (deep(qq) - 7) as i32
}
fn main() { std::process::exit(logos_main()); }
