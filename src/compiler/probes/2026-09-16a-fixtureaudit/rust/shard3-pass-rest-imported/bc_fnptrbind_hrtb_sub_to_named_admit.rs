fn f<'r>(g: for<'x> fn(&'x i64) -> i64) -> fn(&'r i64) -> i64 {
    return g;
}
fn main() {
    let _ = f;
    std::process::exit(0);
}
