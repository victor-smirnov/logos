trait Sh { fn area(&self) -> i64; }
struct Sq { s: i64 }
struct Ci { r: i64 }
impl Sh for Sq { fn area(&self) -> i64 { self.s * self.s } }
impl Sh for Ci { fn area(&self) -> i64 { 3 * self.r * self.r } }
fn main() {
    let n: i64 = 2;
    let x = Sq{s:1}; let y = Ci{r:n}; let b: &dyn Sh = if n == 0 { &x } else { &y };
    println!("{}", b.area());
}
