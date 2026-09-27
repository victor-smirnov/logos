fn inc(xs: &mut [i64]) { for x in xs.iter_mut() { *x += 1; } }
fn main() { let mut arr = [1i64, 2, 3, 4]; inc(&mut arr[1..3]); println!("{:?}", arr); }
