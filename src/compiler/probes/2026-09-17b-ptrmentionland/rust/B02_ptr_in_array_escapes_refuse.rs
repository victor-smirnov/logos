// twin of hand/B02 — ILLEGAL, rustc must REFUSE
fn first<'a>(t: [*const &'a i64; 2]) -> &'a i64 { unsafe { *t[0] } }
fn main() {
    let out: &i64;
    {
        let v: i64 = 5;
        let r: &i64 = &v;
        let p: *const &i64 = &r;
        out = first([p, p]);
    }
    std::process::exit(*out as i32);
}
