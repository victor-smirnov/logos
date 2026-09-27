struct P<const N: usize> { v: [i32; N] }
fn take(p: P<3>) -> i32 { p.v[0] }
fn main() { let p = P::<2> { v: [1, 2] }; println!("{}", take(p)); }
