fn main() {
    let text = String::from("hello x pqr");
    let v: Vec<&str> = text.as_str().split(' ').map(|w| w).collect();
    println!("{}", v.len());
}
