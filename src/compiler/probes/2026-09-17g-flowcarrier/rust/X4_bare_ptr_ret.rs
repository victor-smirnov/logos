fn raw<'a>(q: *const &'a i64) -> &'a i64 { unsafe { *q } }
fn escape() -> &'static i64 {
    let v: i64 = 9;
    let r: &i64 = &v;
    let q: *const &i64 = &r;
    raw(q)
}
fn main() { std::process::exit(*escape() as i32); }
