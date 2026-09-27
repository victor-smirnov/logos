struct S { a: i64, p: [i64; 2] }
fn main() { let s = S { a: 1, p: [2, 3] }; let S { a, p: [x, y] } = &s; println!("{} {} {}", a, x, y); }
