struct D { v: i64 }
fn inner(a: &&D) -> *const D {
    let x: &D = *a;
    return x as *const D;
}
fn main() {
    let arr: [D; 2] = [D { v: 6i64 }, D { v: 7i64 }];
    let s: &[D] = &arr;
    for x in s {
        let want: *const D = x as *const D;
        if inner(&x) != want { std::process::exit(1); }
    }
    std::process::exit(0);
}
