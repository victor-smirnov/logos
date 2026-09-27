fn g(v: &mut [i32]) { if let [first, ..] = v { *first = 9; } }
fn main() { let mut a = [1, 2, 3]; g(&mut a); println!("{}", a[0]); }
