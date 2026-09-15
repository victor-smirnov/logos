fn logos_main() -> i32 {
    let mut a: [i64; 2] = [1i64, 2i64];
    let e: &i64 = &a[1usize];
    a[0usize] = *e + 1i64;
    return (a[0usize] - 3i64) as i32;
}

fn main() { std::process::exit(logos_main()); }
