struct P { v: i64 }
fn main() {
    let it: Box<dyn Iterator<Item = i64>> = Box::new(0..3);
    let b: Vec<P> = vec![P { v: 10 }, P { v: 20 }];
    for (x, p) in it.zip(b.iter()) { println!("{} {}", x, p.v); }
}
