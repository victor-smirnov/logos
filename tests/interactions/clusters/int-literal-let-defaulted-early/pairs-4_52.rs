fn g() -> i64 { 5 }
fn main() {
    let mut t = 0;
    t += g();
    println!("{}", t);
}
