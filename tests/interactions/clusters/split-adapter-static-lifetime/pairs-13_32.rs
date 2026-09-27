fn main() {
    let u = String::from("a bb c");
    let parts: Vec<String> = u.as_str().split(' ').map(|p| p.to_string()).collect();
    println!("{}", parts.len());
}
