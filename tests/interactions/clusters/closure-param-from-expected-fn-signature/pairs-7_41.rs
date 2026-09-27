fn main() {
    let k = 4i64;
    let f: Box<dyn Fn(i64) -> i64> = Box::new(move |x| x + k);
    let o: Option<Box<dyn Fn(i64) -> i64>> = Some(Box::new(move |x| x * k));
    println!("{}", f(1));
    if let Some(g) = &o { println!("{}", g(2)); }
}
