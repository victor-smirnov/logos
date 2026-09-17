fn raw<'a>(q: *const &'a i64) -> i64 { unsafe { **q } }
fn main() {
    let v: i64 = 3;
    let r: &i64 = &v;
    let q: *const &i64 = &r;
    std::process::exit((raw(q) - 3) as i32);
}
