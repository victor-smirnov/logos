fn main() {
    let mut a: [[i64; 2]; 2] = [[1, 2], [3, 4]];
    let e: &i64 = &a[1][0];
    a[0][1] = *e + 1;
    std::process::exit((a[0][1] - 4) as i32);
}
