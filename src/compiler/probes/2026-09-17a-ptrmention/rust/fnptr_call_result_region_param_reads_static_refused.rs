// twin of tests/soundness/open/fnptr_call_result_region_param_reads_static_refused.logos
fn f<'r>(g: fn(&'r i64) -> i64) -> fn(&'r i64) -> i64 { g }
fn rd(x: &i64) -> i64 { *x }
fn main() {
    let h = f(rd);
    let v: i64 = 18;
    std::process::exit(h(&v) as i32);
}
