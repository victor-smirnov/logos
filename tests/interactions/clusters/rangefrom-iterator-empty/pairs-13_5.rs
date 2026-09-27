fn main() {
    let a: Vec<(char, i32)> = "abc".chars().zip(1..).collect();
    let c: Vec<i32> = (5..).take(3).collect();
    println!("{:?} {:?}", a, c);
}
