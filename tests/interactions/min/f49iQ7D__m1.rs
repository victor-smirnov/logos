fn main() {
    let a = [1i64, 2, 3];
    let s: &[i64] = &a[1..];
    println!("{:?}", s);
}
