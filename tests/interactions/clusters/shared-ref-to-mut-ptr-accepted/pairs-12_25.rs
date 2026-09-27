fn main() { let x: i64 = 1; let p = &x as *mut i64; unsafe { *p = 2; } println!("{}", x); }
