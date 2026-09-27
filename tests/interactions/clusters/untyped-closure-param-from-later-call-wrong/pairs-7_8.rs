struct Money { amt: i64 }
fn main() {
    let m = |c| Money { amt: c };
    let a = m(100);
    let b = m(7);
    println!("{} {}", a.amt, b.amt);
}
