fn static_id<'a>(t: &'a i64) -> &'static i64 where 'a: 'static { return t; }
fn f() -> i64 {
    let n: i64 = 1i64;
    let r = static_id(&n);
    return *r;
}
fn main() { std::process::exit(f() as i32); }
