fn main() {
    let bytes: [u8; 3] = [1, 2, 3];
    let v: Vec<i64> = vec![4, 5];
    let o: Option<&i64> = v.iter().next();
    if let Some(&x) = o { println!("if {}", x); }
    match o { Some(&x) => println!("m {}", x), None => {} }
    let mut i = 0usize;
    while let Some(&b) = bytes.get(i) { println!("w {}", b); i += 1; }
}
