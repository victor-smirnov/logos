// twin of hand/D02 — LEGAL
fn same(x: &*mut i64, y: &*mut i64) -> bool { x == y }
fn main() {
    let mut a: i64 = 1;
    let p: *mut i64 = &mut a;
    let q: *mut i64 = p;
    if same(&p, &q) { std::process::exit(0); }
    std::process::exit(1);
}
