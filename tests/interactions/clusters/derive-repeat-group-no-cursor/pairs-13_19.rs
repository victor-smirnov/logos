#[derive(Clone)]
struct V { s: String }
fn main() {
    let a = V { s: String::from("hi") };
    let b = a.clone();
    println!("{} {}", a.s, b.s);
}
