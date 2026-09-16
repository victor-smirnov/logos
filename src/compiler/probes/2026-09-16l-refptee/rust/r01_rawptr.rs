fn same<'a>(x: &*mut &'a i64, y: &*mut &'a i64) -> bool { x == y }
fn main() { let mut a: i64 = 1; let mut ra: &i64 = &a; let p: *mut &i64 = &mut ra; let q = p; std::process::exit(if same(&p, &q) { 0 } else { 1 }); }
