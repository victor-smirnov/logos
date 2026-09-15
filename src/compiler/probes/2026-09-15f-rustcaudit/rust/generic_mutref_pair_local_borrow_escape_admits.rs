struct P<'a> { x: &'a i64, y: &'a i64 }
fn swap2<T>(a: &mut T, b: &mut T) -> i64 { return 0i64; }
fn put(p: &mut P) -> i64 {
    let local: i64 = 9i64;
    let mut q = P { x: p.x, y: &local };
    return swap2(p, &mut q);
}
fn main() {
    let a: i64 = 1i64;
    let mut p = P { x: &a, y: &a };
    std::process::exit(put(&mut p) as i32);
}
