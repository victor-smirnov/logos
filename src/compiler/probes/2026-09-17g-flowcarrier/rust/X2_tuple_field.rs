struct TH<'a> { t: (*const &'a i64, i64) }
fn first<'a>(h: TH<'a>) -> &'a i64 { unsafe { *h.t.0 } }
fn escape() -> &'static i64 {
    let v: i64 = 8;
    let r: &i64 = &v;
    let h = TH { t: (&r, 1) };
    first(h)
}
fn main() { std::process::exit(*escape() as i32); }
