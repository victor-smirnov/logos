fn main() {
    let mut a: [i64; 2] = [7, 0];
    let e: &i64 = &a[1];
    a[*e as usize] = 9;
    std::process::exit((a[0] - 9) as i32);
}
