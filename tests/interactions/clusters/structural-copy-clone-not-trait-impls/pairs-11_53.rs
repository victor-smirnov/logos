fn dup<T: Copy>(x: T) -> (T, T) { (x, x) }
fn main() { let t = dup((1, 'a')); println!("{} {}", (t.0).0, (t.1).1); let u = dup((2i32, 'b')); println!("{}", (u.0).0); }
