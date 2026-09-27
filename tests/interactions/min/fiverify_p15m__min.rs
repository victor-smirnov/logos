struct Cfg { name: String, depth: i64 }
fn main() {
    let c = Cfg { name: String::from("base"), depth: 1 };
    let n = c.name;
    match c { _ => println!("x") }
    println!("{}", n);
}
