fn f(i: usize) -> usize { i * 2 }
fn g(i: u8) -> u8 { i + 1 }
fn main() { let mut s: usize = 0; for i in 0usize..3 { s += f(i); } let n: usize = 4; for i in 0..n { s += f(i); } for j in 1u8..3 { s += g(j) as usize; } println!("{}", s); }
