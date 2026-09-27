#[derive(Clone, Copy)]
enum Op { Add(i64), Mul(i64) }
fn main() {
    let ops = [Op::Add(5), Op::Mul(3), Op::Add(1)];
    for op in &ops { match op { Op::Add(k) => println!("add {}", k), Op::Mul(k) => println!("mul {}", k) } }
    for i in 0..3 { match ops[i] { Op::Add(k) => println!("add {}", k), Op::Mul(k) => println!("mul {}", k) } }
    for op in ops.iter() { match op { Op::Add(k) => println!("add {}", k), Op::Mul(k) => println!("mul {}", k) } }
}
