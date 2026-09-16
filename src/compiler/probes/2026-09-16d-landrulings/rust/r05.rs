fn main() {
    let mut a: i64 = 3;
    let b: i64 = 4;
    let ra: &mut i64 = &mut a;
    let rb: &i64 = &b;
    std::process::exit(if ra < rb { 0 } else { 1 });
}
