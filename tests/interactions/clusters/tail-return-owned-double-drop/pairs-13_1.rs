fn id(x: String) -> String { x }
fn main() {
    let s = id(String::from("hello"));
    println!("{}", s);
}
