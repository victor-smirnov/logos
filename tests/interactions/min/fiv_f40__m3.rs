fn main() {
    let mut a: Vec<Option<i64>> = Vec::new();
    a.push(Some(2));
    println!("k {}", a[0].unwrap());
    let b: Vec<Option<Box<i64>>> = Vec::new();
    println!("{}", b.len());
}
