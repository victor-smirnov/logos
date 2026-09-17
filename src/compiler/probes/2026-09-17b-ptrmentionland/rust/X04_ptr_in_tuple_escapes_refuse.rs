// twin of hand/X04 — ILLEGAL, rustc must REFUSE
fn first<'a>(t: (*const &'a i64, i64)) -> &'a i64 { unsafe { *t.0 } }
fn main() {
    let out: &i64;
    { let v: i64 = 6; let r: &i64 = &v; let p: *const &i64 = &r; out = first((p, 1)); }
    std::process::exit(*out as i32);
}
