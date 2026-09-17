fn keep<'a>(x: &'a i64, y: &'a i64) -> &'a i64 { if *x > *y { x } else { y } }
fn main() {
    let a: i64 = 1;
    let r;
    { let b: i64 = 2; r = keep(&a, &b); }   // E0597: b dropped while borrowed
    println!("{}", r);
}
