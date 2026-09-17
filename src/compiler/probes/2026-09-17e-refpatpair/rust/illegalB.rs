fn get(r: &Option<String>) -> String {
    let &Option::Some(x) = r else { return String::from("n"); };
    x
}
fn main() { let o: Option<String> = Some(String::from("hi")); println!("{}", get(&o)); }
