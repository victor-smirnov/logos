use std::env;
#[inline(never)] fn kernel(r:i64)->i64 {
    let mut x=[0i64;4096]; let mut y=[0i64;4096]; let mut i=0i64;
    while i<4096 { x[i as usize]=i; y[i as usize]=4096-i; i+=1; }
    let mut acc=0i64; let mut k=0i64;
    while k<r {
        i=0; while i<4096 { x[i as usize]+=1; i+=1; }
        let mut s=0i64; i=0; while i<4096 { s += x[i as usize]*y[i as usize]; i+=1; }
        acc ^= s; k+=1;
    } acc }
fn main(){ let r:i64=env::args().nth(1).unwrap_or("1".into()).parse().unwrap();
    std::process::exit((kernel(r)&1) as i32); }
