struct S<'a> { v: &'a i64 }
fn f<'lt>(s: &'lt S<'lt>) -> i64 { return *(s.v); }
fn main() {
    let n: i64 = 42;
    let s = S { v: &n };
    std::process::exit((f(&s) - 42) as i32);
}
