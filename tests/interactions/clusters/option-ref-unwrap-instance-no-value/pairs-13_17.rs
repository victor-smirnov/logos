#[derive(PartialEq, Eq, PartialOrd, Ord, Clone, Copy)]
struct K { a: u8, b: i16 }
fn main() {
    let mut v: Vec<K> = Vec::new();
    v.push(K { a: 2, b: 1 }); v.push(K { a: 9, b: 0 });
    let mx = v.iter().max().unwrap();
    println!("{} {}", mx.a, mx.b);
}
