// twin of tests/soundness/open/refptr_inner_region_elision_demands_static_refused.logos
fn same<'a>(x: &*mut &'a i64, y: &*mut &'a i64) -> bool { x == y }
fn main() {
    let a: i64 = 1;
    let mut ra: &i64 = &a;
    let p: *mut &i64 = &mut ra;
    let q: *mut &i64 = p;
    if same(&p, &q) { std::process::exit(0); }
    std::process::exit(1);
}
