use std::ops::{Add, Sub};
enum Val { I(i64), Nil }
impl Add for Val { type Output = Val; fn add(self, o: Val) -> Val { match (self, o) { (Val::I(a), Val::I(b)) => Val::I(a + b), (x, _) => x } } }
impl Sub for Val { type Output = Val; fn sub(self, o: Val) -> Val { match o { Val::I(b) => self + Val::I(0 - b), Val::Nil => self } } }
fn show(v: &Val) -> i64 { match v { Val::I(a) => *a, Val::Nil => 0 } }
fn main() { let v = Val::I(10) - Val::I(4); println!("{}", show(&v)); let w = Val::I(1) + Val::I(2) + Val::Nil; println!("{}", show(&w)); }
