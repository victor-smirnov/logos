fn apply2<T, F: Fn(T) -> i64>(x: T, f: F) -> i64 { f(x) }
fn main() {
    let k: i64 = 3;
    std::process::exit(apply2(k, |v| v + 1) as i32);
}
