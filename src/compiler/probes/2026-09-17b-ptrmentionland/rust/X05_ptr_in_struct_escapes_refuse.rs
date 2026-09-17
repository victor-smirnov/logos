// twin of hand/X05 — ILLEGAL, rustc must REFUSE
struct Holder<'a> { q: *const &'a i64 }
fn first<'a>(h: Holder<'a>) -> &'a i64 { unsafe { *h.q } }
fn main() {
    let out: &i64;
    { let v: i64 = 7; let r: &i64 = &v; let p: *const &i64 = &r; out = first(Holder { q: p }); }
    std::process::exit(*out as i32);
}
