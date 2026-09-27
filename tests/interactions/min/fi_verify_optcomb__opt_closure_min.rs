fn main() {
    let k = 10i64;
    let a: Option<i64> = None;
    let v = a.unwrap_or_else(|| k + 1);
    std::process::exit(if v == 11 { 0 } else { 1 });
}
