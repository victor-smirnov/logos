use std::env;
#[inline(never)] fn fib(n: i64) -> i64 { if n < 2 { return n; } fib(n-1) + fib(n-2) }
#[inline(never)] fn kernel(r: i64) -> i64 { let mut acc=0i64; let mut k=0i64;
    while k < r { acc ^= fib(18 + (k & 3)); k += 1; } acc }
fn main(){ let r:i64=env::args().nth(1).unwrap_or("1".into()).parse().unwrap();
    std::process::exit((kernel(r)&1) as i32); }
