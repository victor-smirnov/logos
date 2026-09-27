struct D(i32);
impl Drop for D { fn drop(&mut self) { println!("d{}", self.0); } }
fn main() {
    let f = |d: D| d.0 + 1;
    let x = f(D(7));
    println!("x{}", x);
}
