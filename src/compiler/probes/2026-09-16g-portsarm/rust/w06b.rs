// STEP C twin: array-indexed store whose INDEX reads a live shared loan.
// a[1] is 0, so the computed index is in bounds and the program runs clean.
fn main() {
    let mut a: [i64; 2] = [7i64, 0i64];
    let e: &i64 = &a[1];
    a[*e as usize] = 9i64;
    std::process::exit((a[0] - 9i64) as i32);
}
