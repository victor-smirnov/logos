struct V<const N: usize> { x: i64 }
fn main() { let a: V<3> = V { x: 1 }; let b: V<2> = a; }
