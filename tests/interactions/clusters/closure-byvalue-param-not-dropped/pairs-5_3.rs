

struct D { id: i64 }
impl Drop for D { fn drop(&mut self) { println!("drop {}", self.id); } }
fn main() {
    let c = |x: D| x.id * 10;
    let d = D { id: 4 };
    println!("r {}", c(d));
    let e = |x: D| -> i64 { x.id * 10 };
    let d2 = D { id: 5 };
    println!("r {}", e(d2));
    println!("end");
}
