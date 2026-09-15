fn id(p: &i64) -> &i64 {
    return p;
}
fn logos_main() -> i32 {
    let a = id(&5i64);
    if *a != 5i64 { return 1i32; }
    return 0i32;
}

fn main() { std::process::exit(logos_main()); }
