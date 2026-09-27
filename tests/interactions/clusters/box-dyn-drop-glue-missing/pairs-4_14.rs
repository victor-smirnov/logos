trait R { fn n(&self) -> usize; }
struct T { s: String }
impl R for T { fn n(&self) -> usize { self.s.len() } }
struct D { k: i64 }
impl Drop for D { fn drop(&mut self) { println!("drop {}", self.k); } }
struct U { d: D }
impl R for U { fn n(&self) -> usize { self.d.k as usize } }
fn main() {
    let b: Box<dyn R> = Box::new(T { s: String::from("hello") });
    println!("{}", b.n());
    let mut v: Vec<Box<dyn R>> = Vec::new();
    v.push(Box::new(T { s: String::from("abc") }));
    v.push(Box::new(U { d: D { k: 7 } }));
    println!("{}", v[0].n() + v[1].n());
    let c: Box<dyn R> = Box::new(U { d: D { k: 9 } });
    println!("{}", c.n());
}
