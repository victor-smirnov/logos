fn main() {
    let mut a: [i64; 3] = [1, 0, 5];
    let e: &i64 = &a[1];
    a[*e as usize] = *e + 4;
    std::process::exit((a[0] - 4) as i32);
}
