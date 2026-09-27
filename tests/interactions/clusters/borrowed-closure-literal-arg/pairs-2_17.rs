fn call(f: &dyn Fn(i64) -> i64) -> i64 { return f(2); }
fn main() {
    println!("{}", call(&|x: i64| x + 1));
}
