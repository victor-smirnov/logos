#[derive(Debug)]
struct Px(u8, u8);
fn main() {
    let a = [1i32, 2, 3];
    let s: &[i32] = &a[1..];
    println!("{:?}", s);
    println!("{:?}", Px(1, 2));
}
