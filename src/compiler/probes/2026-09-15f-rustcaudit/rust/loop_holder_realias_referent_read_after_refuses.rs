fn logos_main() -> i32 {
    let mut a: i64 = 0i64;
    let mut b: i64 = 100i64;
    let mut x: &mut i64 = &mut a;
    let mut i: i64 = 0i64;
    while i < 3i64 {
        *x = *x + 1i64;
        x = &mut b;
        i = i + 1i64;
    }
    return (a + b - 103i64) as i32;
}

fn main() { std::process::exit(logos_main()); }
