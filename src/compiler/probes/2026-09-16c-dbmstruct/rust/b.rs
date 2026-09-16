fn main(){ let mut v: Vec<i64> = Vec::new(); v.push(2); v.push(0); { let e: &i64 = &v[0usize]; let i = *e as usize; } v[0usize] = 2; std::process::exit(v[0usize] as i32); }
