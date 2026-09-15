struct P { a: i64, b: i64 }
fn main() {
    let mut s1: P = P { a: 1i64, b: 2i64 };
    let r: &mut P = &mut s1;
    let a: &mut i64 = &mut (*r).a;
    r.a = 5i64;
    *a = 7i64;
    std::process::exit(0);
}
