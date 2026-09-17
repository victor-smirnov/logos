// twin of hand/G02_ptr_in_array_param.logos — LEGAL
fn both<'a>(t: [*const &'a i64; 2]) -> i64 { unsafe { **t[0] + **t[1] } }
fn main() {
    let v: i64 = 5;
    let r: &i64 = &v;
    let p: *const &i64 = &r;
    std::process::exit((both([p, p]) - 10) as i32);
}
