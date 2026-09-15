struct T {
    z: i64,
}
struct W<T> {
    v: T,
}
fn logos_main() -> i32 {
    let w = W { v: 5i64 };
    let t = T { z: 1i64 };
    if w.v + t.z != 6i64 { return 1i32; }
    return 0i32;
}

fn main() { std::process::exit(logos_main()); }
