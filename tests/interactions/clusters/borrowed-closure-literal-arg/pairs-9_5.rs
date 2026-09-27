fn ap2(v: i64, f: &dyn Fn(i64) -> i64) -> i64 { f(v) }
fn main() {
    println!("{}", ap2(4, &|x: i64| x * 10));
}
