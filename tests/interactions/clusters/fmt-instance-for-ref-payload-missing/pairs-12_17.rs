fn main() {
    let s = String::from("a");
    let o: Option<&String> = Some(&s);
    println!("{:?}", o);
}
