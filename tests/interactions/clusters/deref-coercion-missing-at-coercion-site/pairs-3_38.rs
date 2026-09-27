fn main() {
    let b: Box<i64> = Box::new(5);
    let other: i64 = 1;
    let mut r: &i64 = &other;
    println!("{}", r);
    r = &b;
    println!("{}", r);
}
