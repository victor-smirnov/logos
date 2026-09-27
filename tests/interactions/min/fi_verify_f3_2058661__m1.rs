struct D { id: i64 }
impl Drop for D { fn drop(&mut self) { println!("drop {}", self.id); } }
fn main() {
    let c = |x: D| x.id; let r = c(D { id: 1 }); println!("r {}", r);
    let c = |_x: D| 7; let r = c(D { id: 4 }); println!("r {}", r);
    let c = |_x: D| (); c(D { id: 5 });
    println!("end");
}
