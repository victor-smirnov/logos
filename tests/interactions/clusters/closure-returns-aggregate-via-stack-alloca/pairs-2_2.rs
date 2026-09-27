fn main() {
    let h = |x: i64| (x, 7i64);
    let s = h(3);
    println!("{} {}", s.0, s.1);
}
