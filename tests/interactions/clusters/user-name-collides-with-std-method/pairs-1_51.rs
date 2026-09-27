fn pow<T: std::ops::Mul<Output = T> + Copy>(b: T, e: i64, one: T) -> T { let mut r = one; let mut i = 0i64; while i < e { r = r * b; i += 1; } return r; }
fn main() { println!("{}", pow(2i64, 10, 1i64)); }
