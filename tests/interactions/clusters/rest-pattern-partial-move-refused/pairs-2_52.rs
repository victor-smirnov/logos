struct Cfg { name: String, depth: i64 }
fn main() {
    let c = Cfg { name: String::from("base"), depth: 1 };
    let n = c.name;
    if let Cfg { depth: 1, .. } = c { println!("one {}", n); }
    match c { Cfg { depth, .. } => println!("{}", depth) }
}
