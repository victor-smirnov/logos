fn dup<T: Copy>(x: T) -> (T, T) { return (x, x); }
fn main() {
    let a: Option<i64> = Some(1);
    let b = a;
    println!("{:?} {:?}", a, b);
    let (c, d) = dup(a);
    println!("{:?} {:?}", c, d);
}
