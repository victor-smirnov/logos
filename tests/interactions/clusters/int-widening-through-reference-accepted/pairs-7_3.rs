fn main() {
    let v: Vec<u8> = vec![1u8, 2, 3, 4, 5, 6, 7, 8];
    let x: &i64 = &v[0];
    println!("{}", x);
    let a: u8 = 7;
    let y: &i64 = &a;
    println!("{}", y);
}
