fn main() {
    let v: i64 = 6;
    let base: *const i64 = &v;
    let f = || -> i64 { unsafe { *base.add(0) } };
    std::process::exit(f() as i32);
}
