fn logos_main() -> i32 {
    let mut a: i64 = 1i64;
    let mut b: i64 = 2i64;
    let mut r: &mut i64 = &mut a;
    let mut i: i64 = 0i64;
    while i < 4i64 {
        *r = *r + 1i64;
        if i < 2i64 { r = &mut b; } else { r = &mut a; }
        i = i + 1i64;
    }
    return 0i32;
}

fn main() { std::process::exit(logos_main()); }
