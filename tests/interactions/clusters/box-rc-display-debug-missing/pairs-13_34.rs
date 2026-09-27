fn main() {
    let b = Box::new(5i32);
    println!("{}", b);
    let c: Box<String> = Box::new(String::from("s"));
    println!("{} {:?}", c, b);
}
