fn main() {
    let a = (1i64, 2i64); let b = (1i64, 3i64);
    println!("{} {}", a < b, a == b);
    let mut v: Vec<(i64, i64)> = Vec::new(); v.push(b); v.push(a);
    v.sort();
    println!("{:?}", v);
}
