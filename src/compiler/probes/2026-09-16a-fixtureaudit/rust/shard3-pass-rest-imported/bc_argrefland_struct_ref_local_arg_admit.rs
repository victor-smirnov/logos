struct D { v: i64 }
fn inner2(a: &&D) -> *const D {
    let x: &D = *a;
    return x as *const D;
}
fn inner<T>(a: &&T) -> *const T {
    let x: &T = *a;
    return x as *const T;
}
fn main() {
    let d: D = D { v: 6i64 };
    let rd: &D = &d;
    let want: *const D = rd as *const D;
    let got: *const D = inner2(&rd);
    if got != want { std::process::exit(1); }
    let got2: *const D = inner::<D>(&rd);
    if got2 != want { std::process::exit(2); }
    std::process::exit(0);
}
