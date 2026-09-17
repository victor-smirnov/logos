// ILLEGAL TWIN (abuse direction): the callee's 'a under a raw pointer is tied to
// a LOCAL, and the returned reference outlives it. rustc must REFUSE.
fn raw<'a>(q: *mut &'a i64) -> &'a i64 { unsafe { *q } }
fn main() {
    let out: &i64;
    {
        let v: i64 = 3;
        let mut r: &i64 = &v;
        let q: *mut &i64 = &mut r;
        out = raw(q);
    }
    std::process::exit(*out as i32);
}
