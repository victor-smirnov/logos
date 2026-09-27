#[derive(Clone, Copy)]
#[derive(Debug)]
struct S<'a> { r: &'a i64 }
fn main() { let x = 1i64; let s = S { r: &x }; let t = s; println!("{} {} {:?}", s.r, t.r, t); }
