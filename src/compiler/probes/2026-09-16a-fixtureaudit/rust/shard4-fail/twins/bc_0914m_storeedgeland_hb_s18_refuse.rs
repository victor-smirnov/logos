#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
struct C { n: i64 }
impl C {
    fn bump(self: &mut C) { self.n = self.n + 1i64; }
}
struct W<U> { x: U }
fn wire<'a, 'b>(t: &mut W<&'a mut Vec<&'b C>>, v: &'a mut Vec<&'b C>) {
    t.x = v;
}
fn lmain() -> i32 {
    let mut c: C = C { n: 1i64 };
    let mut other: Vec<&C> = Vec::new();
    let mut vs: Vec<&C> = Vec::new();
    let mut t: W<&mut Vec<&C>> = W { x: &mut other };
    wire(&mut t, &mut vs);
    t.x.push(&c);
    c.bump();
    return vs.len() as i32;
}
fn main() {}
