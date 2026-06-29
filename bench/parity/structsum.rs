use std::env;
#[derive(Clone,Copy)] struct P { x:i64, y:i64 }
#[inline(never)] fn kernel(r:i64)->i64 {
    let mut a=[P{x:0,y:0}; 2048]; let mut i=0i64;
    while i<2048 { a[i as usize]=P{x:i,y:-i}; i+=1; }
    let mut acc=0i64; let mut k=0i64;
    while k<r {
        i=0; while i<2048 { a[i as usize].x += 1; i+=1; }
        let mut s=0i64; i=0; while i<2048 { s += a[i as usize].x - a[i as usize].y; i+=1; }
        acc ^= s; k+=1;
    } acc }
fn main(){ let r:i64=env::args().nth(1).unwrap_or("1".into()).parse().unwrap();
    std::process::exit((kernel(r)&1) as i32); }
