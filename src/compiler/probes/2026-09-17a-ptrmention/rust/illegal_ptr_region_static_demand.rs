// ILLEGAL TWIN: callee genuinely demands 'static under the raw pointer;
// a local borrow must NOT satisfy it. rustc must REFUSE.
fn needs_static(q: *mut &'static i64) -> i64 { unsafe { **q } }
fn main() {
    let v: i64 = 3;
    let mut r: &i64 = &v;
    let q: *mut &i64 = &mut r;
    std::process::exit(needs_static(q) as i32);
}
