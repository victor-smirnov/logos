fn first<'a>(t: ([*mut &'a i64; 1], i64)) -> i64 { unsafe { **(t.0[0]) } }
fn logos_main() -> i32 { let v: i64 = 9; let mut r: &i64 = &v; let p: *mut &i64 = &mut r; (first(([p], 1)) - 9) as i32 }
fn main() { std::process::exit(logos_main()); }
