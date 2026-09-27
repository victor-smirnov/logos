fn main() {
    let s = String::from("ab cd");
    let parts: Vec<&str> = s.split(' ').collect();
    println!("{}", parts.len());
    let n = s.chars().count();
    println!("{}", n);
    let t = s.as_str();
    println!("{}", t.chars().count());
}
