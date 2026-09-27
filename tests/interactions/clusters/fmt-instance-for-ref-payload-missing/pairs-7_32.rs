fn main() {
    let a = 1i64; let b = 2i64;
    let mut refs: Vec<&i64> = Vec::new();
    refs.push(&a); refs.push(&b);
    println!("{:?}", refs);
}
