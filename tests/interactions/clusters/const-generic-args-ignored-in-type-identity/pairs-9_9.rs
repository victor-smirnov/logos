struct V<const N: usize> { c: [i64; N] }
fn take(v: V<2>) -> i64 { v.c[0] }
fn main() { let a: V<3> = V { c: [1, 2, 3] }; println!("{}", take(a)); }
