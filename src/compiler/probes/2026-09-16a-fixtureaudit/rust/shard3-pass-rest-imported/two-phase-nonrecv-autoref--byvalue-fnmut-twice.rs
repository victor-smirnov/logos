fn twice_ten_om<F: FnMut(i64) -> i64>(mut f: F) -> i64 {
    return f(f(10i64));
}
fn main() { let _ = twice_ten_om(|x| x + 1i64); }
