trait R { fn n(&self) -> i64; }
struct D { k: i64 }
impl Drop for D { fn drop(&mut self) { println!("drop {}", self.k); } }
impl R for D { fn n(&self) -> i64 { self.k } }
fn main() {
    let mut v: Vec<Box<dyn R>> = Vec::new();
    v.push(Box::new(D { k: 7 }));
    let d: Box<dyn R> = Box::new(D { k: 5 });
    println!("{}", d.n() + v.len() as i64);
}
