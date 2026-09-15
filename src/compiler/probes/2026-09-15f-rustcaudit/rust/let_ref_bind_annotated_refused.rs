fn logos_main() -> i32 {
    let x: i64 = 7i64;
    let ref y: i64 = x;
    let ref z: i64 = 5i64;
    if *y + *z != 12i64 { return 1i32; }
    return 0i32;
}

fn main() { std::process::exit(logos_main()); }
