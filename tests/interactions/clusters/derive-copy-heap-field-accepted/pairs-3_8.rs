#[derive(Clone, Copy)]
struct S { v: Vec<i64>, t: (i64, i64) }
fn main() { let a = S { v: vec![1], t: (1, 2) }; let b = a; println!("{}", a.t.0 + b.t.1); }
