fn first<T>(a: T, b: T) -> T { return a; }
fn go<'a, 'b>(x: &'a i64, y: &'b i64) -> i64 {
    let r = first(x, y);
    return *r;
}
fn logos_main() -> i32 {
    let a: i64 = 6i64;
    let b: i64 = 7i64;
    return go(&a, &b) as i32;
}

fn main() { std::process::exit(logos_main()); }
