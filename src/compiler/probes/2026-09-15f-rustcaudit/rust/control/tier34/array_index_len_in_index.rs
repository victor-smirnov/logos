fn logos_main() -> i32 {
    let mut a: [i64; 2] = [1i64, 2i64];
    a[a.len() - 1usize] = 5i64;
    return (a[1usize] - 5i64) as i32;
}
fn main() { std::process::exit(logos_main()); }
