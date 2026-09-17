fn main() {
    let mut x: i64 = 1; let y: i64 = 2;
    let ra: &mut i64 = &mut x; let rb: &i64 = &y;
    if ra < rb { std::process::exit(1); }
}
