unsafe fn launder(p: &*const i64) -> i64 { let q: *mut i64 = *p; unsafe { *q = 99; *q } }
fn main() { let v: i64 = 7; let p: *const i64 = &v; std::process::exit((unsafe { launder(&p) } - 99) as i32); }
