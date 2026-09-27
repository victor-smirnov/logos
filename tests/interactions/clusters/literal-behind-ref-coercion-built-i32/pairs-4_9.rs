fn f2(items: &[i64]) -> i64 { let mut s: i64 = 0; for x in items { s = s * 100 + *x; } s }
struct S;
impl S { fn st(items: &[i64]) -> i64 { f2(items) } fn me(&self, items: &[i64]) -> i64 { f2(items) } }
trait T { fn tm(&self, items: &[i64]) -> i64; }
impl T for S { fn tm(&self, items: &[i64]) -> i64 { f2(items) } }
fn main() {
    println!("{}", S::st(&[1, 5, 9]));
    println!("{}", S.me(&[1, 5, 9]));
    println!("{}", S.tm(&[1, 5, 9]));
    let v: i64 = S::st(&[4, 4]); println!("{}", v);
}
