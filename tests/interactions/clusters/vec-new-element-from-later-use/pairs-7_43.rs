fn main() {
    let mut seen = Vec::new();
    let x: i64 = 4;
    if let Some(last) = seen.last() { if *last == x { println!("dup"); } }
    seen.push(x);
    println!("{:?}", seen);
}
