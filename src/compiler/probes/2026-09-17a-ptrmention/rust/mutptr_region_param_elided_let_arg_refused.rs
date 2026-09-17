// twin of tests/soundness/open/mutptr_region_param_elided_let_arg_refused.logos
fn raw<'a>(q: *mut &'a i64) -> i64 { unsafe { **q } }
fn main() {
    let v: i64 = 3;
    let mut r: &i64 = &v;
    let q: *mut &i64 = &mut r;
    std::process::exit((raw(q) - 3) as i32);
}
