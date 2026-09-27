fn rd(p: *const i64) -> i64 { unsafe { *p } }
fn reader(p: *const i64) -> impl Fn() -> i64 { move || unsafe { *p } }
fn main() {
    let mut x: i64 = 10;
    let px: *mut i64 = &mut x;
    let pc: *const i64 = px as *const i64;
    let r = reader(pc);
    unsafe { *px = 44; }
    println!("{} {} {}", r(), rd(pc), x);
}
