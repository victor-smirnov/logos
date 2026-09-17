// twin of hand/G01_ptr_to_ptr.logos — LEGAL
fn deep<'a>(q: *mut *mut &'a i64) -> i64 { unsafe { ***q } }
fn main() {
    let v: i64 = 7;
    let mut r: &i64 = &v;
    let mut p: *mut &i64 = &mut r;
    let qq: *mut *mut &i64 = &mut p;
    std::process::exit((deep(qq) - 7) as i32);
}
