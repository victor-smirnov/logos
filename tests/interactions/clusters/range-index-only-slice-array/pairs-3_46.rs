fn main() { let mut a: [i64; 3] = [1, 2, 3]; { let sl: &mut [i64] = &mut a[..2]; sl[0] += 10; } let v = &mut a[1..]; v[1] = 7; println!("{:?}", a); }
