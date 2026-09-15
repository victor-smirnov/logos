fn get(r: &Option<i64>) -> i64 {
    let &Option::Some(x) = r else { return 77i64; };
    return x;
}
fn logos_main() -> i32 {
    let o: Option<i64> = Option::Some(5i64);
    if get(&o) != 5i64 { return 1i32; }
    let n: Option<i64> = Option::None;
    if get(&n) != 77i64 { return 2i32; }
    return 0i32;
}

fn main() { std::process::exit(logos_main()); }
