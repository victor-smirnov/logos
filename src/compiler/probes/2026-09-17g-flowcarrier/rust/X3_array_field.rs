struct AH<'a> { a: [*const &'a i64; 2] }
fn first<'a>(h: AH<'a>) -> &'a i64 { unsafe { *h.a[0] } }
fn escape() -> &'static i64 {
    let v: i64 = 6;
    let r: &i64 = &v;
    let h = AH { a: [&r, &r] };
    first(h)
}
fn main() { std::process::exit(*escape() as i32); }
