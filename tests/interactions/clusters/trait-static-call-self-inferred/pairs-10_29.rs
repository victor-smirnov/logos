#[derive(Debug, Clone, Default)]
struct Cfg { a: i64, b: bool, z: i64 }
fn main() {
    let c = Cfg { a: 5, ..Default::default() };
    println!("{:?}", c);
}
