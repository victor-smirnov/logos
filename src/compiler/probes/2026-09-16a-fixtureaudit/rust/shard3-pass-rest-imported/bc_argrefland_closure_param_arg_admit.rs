struct D { v: i64 }
fn inner(a: &&D) -> *const D {
    let x: &D = *a;
    return x as *const D;
}
fn main() {
    let d: D = D { v: 6i64 };
    let want: *const D = &d as *const D;
    let f = |rd: &D| -> bool { return inner(&rd) == want; };
    if !f(&d) { std::process::exit(1); }
    std::process::exit(0);
}
