fn main() {
    let f: Box<dyn Fn() -> i64> = Box::new(|| 100);
    let g: Box<dyn Fn(i64) -> i64> = Box::new(|x| x + 1);
    println!("{} {}", f(), g(1));
}
