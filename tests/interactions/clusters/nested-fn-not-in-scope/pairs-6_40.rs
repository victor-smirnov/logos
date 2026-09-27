fn main() {
    let a = 3;
    println!("{}", a);
    fn sum(x: i32) -> i32 { if x == 0 { 0 } else { x + sum(x - 1) } }
    println!("{}", sum(a));
}
