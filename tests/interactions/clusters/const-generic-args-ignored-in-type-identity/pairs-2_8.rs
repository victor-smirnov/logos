struct Ring<const N: usize> { d: [i64; N] }
fn main() { let a: Ring<4> = Ring { d: [1, 2, 3, 4] }; let r: Ring<3> = a; println!("{}", r.d.len()); }
