fn main() {
    let mut t: (i64, i64) = (3, 30);
    let x = t;
    t = (1, 10);
    let _ = t;
    std::process::exit(x.0 as i32);
}
