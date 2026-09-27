fn tot<const M: usize>(a: &[i32; M]) -> i32 { let mut t: i32 = 0; for x in a.iter() { t += *x; } return t; }
fn main() { let a = [1i32, 2, 3]; println!("{}", tot(&a)); }
