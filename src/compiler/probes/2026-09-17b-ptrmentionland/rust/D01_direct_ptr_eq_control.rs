// twin of hand/D01 — LEGAL
fn main() {
    let a: i64 = 1;
    let mut ra: &i64 = &a;
    let p: *mut &i64 = &mut ra;
    let q: *mut &i64 = p;
    if p == q { std::process::exit(0); }
    std::process::exit(1);
}
