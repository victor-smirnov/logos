// twin of hand/B01 — ILLEGAL, rustc must REFUSE
fn thru<'r>(g: fn(&'r i64) -> &'r i64, x: &'r i64) -> &'r i64 { g(x) }
fn id(x: &i64) -> &i64 { x }
fn main() {
    let out: &i64;
    { let v: i64 = 3; out = thru(id, &v); }
    std::process::exit(*out as i32);
}
