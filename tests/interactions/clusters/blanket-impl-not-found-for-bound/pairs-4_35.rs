trait Visitor { fn visit(&mut self, v: i64); }
impl<F: FnMut(i64)> Visitor for F { fn visit(&mut self, v: i64) { self(v) } }
fn walk<V: Visitor>(v: &mut V) { for i in 1..4 { v.visit(i); } }
fn main() {
    let mut acc: i64 = 0;
    let mut vis = |v: i64| { acc += v; };
    walk(&mut vis);
    println!("{}", acc);
}
