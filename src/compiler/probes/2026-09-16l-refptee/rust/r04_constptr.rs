fn read(p: &*const i64) -> i64 { unsafe { **p } }
fn main() { let v: i64 = 7; let p: *const i64 = &v; std::process::exit((read(&p) - 7) as i32); }
