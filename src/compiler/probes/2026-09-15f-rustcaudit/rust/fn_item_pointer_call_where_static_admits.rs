fn static_id<'a>(t: &'a i64) -> i64 where 'a: 'static { return *t; }
fn f(u: &i64) -> i64 {
    let g = static_id;
    return g(u);
}
fn main() { let n = 1i64; std::process::exit(f(&n) as i32); }
