static TABLE: [i64; 4] = [1, 10, 100, 1000];
fn main() { println!("{} {}", TABLE[1], TABLE[3]); let f = |i: usize| TABLE[i]; println!("{}", f(2)); }
