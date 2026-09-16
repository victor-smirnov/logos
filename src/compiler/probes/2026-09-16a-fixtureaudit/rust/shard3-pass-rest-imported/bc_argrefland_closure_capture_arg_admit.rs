struct D { v: i64 }
fn inner(a: &&D) -> *const D {
    let x: &D = *a;
    return x as *const D;
}
fn main() {
    let d: D = D { v: 6i64 };
    let rd: &D = &d;
    let want: *const D = rd as *const D;
    let f = || -> bool { return inner(&rd) == want; };
    if !f() { std::process::exit(1); }
    std::process::exit(0);
}
