fn g(v: &mut [i32]) { if let [x, ..] = v { *x = 9; } }
fn main() { let mut a: [i32; 3] = [1, 2, 3]; g(&mut a); println!("{}", a[0]); }
