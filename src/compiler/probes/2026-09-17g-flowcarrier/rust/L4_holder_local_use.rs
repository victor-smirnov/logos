struct Holder<'a> { q: *const &'a i64 }
fn main() {
    let v: i64 = 5;
    let r: &i64 = &v;
    let h = Holder { q: &r };
    let out = unsafe { *h.q };
    std::process::exit((*out - 5) as i32);
}
