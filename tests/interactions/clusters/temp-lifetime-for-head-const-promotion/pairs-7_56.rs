#[derive(Clone, Copy)]
enum Op { Add, Sub }
const OPS: [Op; 2] = [Op::Add, Op::Sub];
const NS: [i64; 2] = [5, 6];
fn main() {
    for op in OPS.iter() { match op { Op::Add => println!("add"), Op::Sub => println!("sub") } }
    let s: i64 = NS.iter().sum();
    println!("{}", s);
}
