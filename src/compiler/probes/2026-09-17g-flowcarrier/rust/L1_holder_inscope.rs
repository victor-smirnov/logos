struct Holder<'a> { q: *const &'a i64 }
fn first<'a>(h: Holder<'a>) -> &'a i64 { unsafe { *h.q } }
fn main() {
    let v: i64 = 4;
    let r: &i64 = &v;
    let h = Holder { q: &r };
    let out = first(h);
    std::process::exit((*out - 4) as i32);
}
