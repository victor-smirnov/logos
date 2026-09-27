fn main() {
    let x = [5i64, 6];
    let base: *const i64 = &x[0];
    let f = |i: usize| -> i64 { unsafe { *base.add(i) } };
    std::process::exit(f(1) as i32);
}
