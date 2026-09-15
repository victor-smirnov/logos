struct P { a: i64, b: i64 }
fn main() {
    let mut s1: P = P { a: 1i64, b: 2i64 };
    let mut s2: P = P { a: 3i64, b: 4i64 };
    let mut r: &mut P = &mut s1;
    let mut i: i64 = 0i64;
    while i < 2i64 {
        let a: &mut i64 = &mut r.a;
        let z: &mut P = &mut s2;
        *a = *a + 1i64;
        r = z;
        i = i + 1i64;
    }
    std::process::exit(0);
}
