fn main() {
    let b: Box<dyn Iterator<Item = i64>> = Box::new(0..4);
    let s: i64 = b.map(|x| x * 3).sum();
    println!("{}", s);
}
