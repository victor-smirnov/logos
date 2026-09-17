struct Holder<'a> { q: *const &'a i64 }
fn first<'a>(h: Holder<'a>) -> &'a i64 { unsafe { *h.q } }
fn escape() -> &'static i64 {
    let v: i64 = 7;
    let r: &i64 = &v;
    let h = Holder { q: &r };
    first(h)
}
fn main() { std::process::exit(*escape() as i32); }
