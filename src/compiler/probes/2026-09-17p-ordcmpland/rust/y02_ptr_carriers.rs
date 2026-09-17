struct H { p: *const i64 }
fn cmp_param(a: *const i64, b: *const i64) -> bool { a < b }
fn mk(a: *const i64) -> *const i64 { a }
fn main() {
    let arr: [i64; 4] = [1, 2, 3, 4];
    let lo: *const i64 = &arr[0] as *const i64;
    let hi: *const i64 = &arr[3] as *const i64;
    let h1 = H { p: lo }; let h2 = H { p: hi };
    let f_field = h1.p < h2.p;
    let t1: (*const i64, i64) = (lo, 1); let t2: (*const i64, i64) = (hi, 1);
    let f_tuple = t1.0 < t2.0;
    let pa: [*const i64; 2] = [lo, hi];
    let f_arr = pa[0] < pa[1];
    let f_param = cmp_param(lo, hi);
    let f_ret = mk(lo) < mk(hi);
    println!("field={} tuple={} arr={} param={} ret={}",
        f_field as i32, f_tuple as i32, f_arr as i32, f_param as i32, f_ret as i32);
    if !f_field { std::process::exit(1); }
    if !f_tuple { std::process::exit(2); }
    if !f_arr { std::process::exit(3); }
    if !f_param { std::process::exit(4); }
    if !f_ret { std::process::exit(5); }
}
