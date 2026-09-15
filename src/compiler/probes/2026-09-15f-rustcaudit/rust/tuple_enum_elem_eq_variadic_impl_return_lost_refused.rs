fn logos_main() -> i32 {
    let a: (Option<i64>, i64) = (Option::Some(2i64), 2i64);
    let b: (Option<i64>, i64) = (Option::Some(2i64), 2i64);
    if !(a == b) { return 1i32; }
    return 0i32;
}

fn main() { std::process::exit(logos_main()); }
