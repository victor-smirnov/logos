fn h(x: &mut i64) { *x += 1; }
fn g(out: &mut i64) { let mut c = || h(out); c(); c(); }
fn main() { let mut o: i64 = 0; g(&mut o); println!("{}", o); }
