fn less<T: Ord>(a: &T, b: &T) -> bool {
    return a < b;
}
fn logos_main() -> i32 {
    let x: i64 = 3i64;
    let y: i64 = 8i64;
    if less::<i64>(&y, &x) { return 1i32; }
    if !less::<i64>(&x, &y) { return 2i32; }
    return 0i32;
}

fn main() { std::process::exit(logos_main()); }
