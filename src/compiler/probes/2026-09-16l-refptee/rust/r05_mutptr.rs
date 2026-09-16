fn bump(p: &mut *mut i64) { unsafe { **p += 1; } }
fn main() { let mut v: i64 = 4; let mut p: *mut i64 = &mut v; bump(&mut p); std::process::exit((v - 5) as i32); }
