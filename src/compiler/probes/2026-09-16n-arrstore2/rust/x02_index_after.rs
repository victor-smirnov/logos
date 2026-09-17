fn main() {
    let mut a: [i64; 2] = [7, 1];
    let e: &i64 = &a[1];
    a[*e as usize] = 9;
    let z: i64 = *e;
    std::process::exit((z - 1) as i32);
}
