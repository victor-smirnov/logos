struct D { v: i64 }
impl Drop for D { fn drop(&mut self) { println!("drop {}", self.v); } }
fn main() {
    let f = |a: D| a.v * 2;
    println!("{}", f(D { v: 4 }));
    println!("end");
}
