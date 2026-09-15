fn b<F: FnMut(i64, i64) -> i64>(f: F) {
    f(1i64, 2i64);
}
fn main() {}
