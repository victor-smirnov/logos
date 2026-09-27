fn main() {
    let b = Box::new(vec![1, 2, 3]);
    let s = { *b };
    println!("{}", s.len());
}
