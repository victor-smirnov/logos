fn main() {
    let x: i64 = 7i64;
    let out: i64 = match x {
        Some(r) => r,
        None => 0i64,
    };
    std::process::exit(out as i32);
}
