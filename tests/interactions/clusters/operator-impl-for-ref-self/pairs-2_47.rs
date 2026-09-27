use std::ops::Mul;
struct Big { v: i64, s: String }
impl Mul<i64> for &Big { type Output = Big; fn mul(self, k: i64) -> Big { Big { v: self.v * k, s: self.s.clone() } } }
fn main() {
    let a = Big { v: 2, s: String::from("x") };
    let r = &a * 3;
    println!("{} {} {}", r.v, r.s, a.v);
}
