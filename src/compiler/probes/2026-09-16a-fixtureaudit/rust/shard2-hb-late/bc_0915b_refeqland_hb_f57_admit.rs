fn same(a: &(i64, i64), b: &(i64, i64)) -> bool { a <= b && a >= b }
fn run() -> i32 {
    let x: (i64, i64) = (2, 3); let y: (i64, i64) = (2, 3); let z: (i64, i64) = (2, 4);
    if !same(&x, &y) { return 1; }
    if same(&x, &z) { return 2; }
    if !(&x < &z) { return 3; }
    0
}
fn main() { std::process::exit(run()); }
