struct S { xs: [i64; 2] }
fn main() {
    let mut s = S { xs: [1, 2] };
    let e: &i64 = &s.xs[1];
    s.xs[0] = *e + 1;
    std::process::exit((s.xs[0] - 3) as i32);
}
