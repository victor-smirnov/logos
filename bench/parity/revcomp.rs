use std::env;
#[inline(never)] fn kernel(r:i64)->i64 {
    let n=16384i64;
    let mut comp=[0u8;256]; let mut i=0i64;
    while i<256 { comp[i as usize]=i as u8; i+=1; }
    comp[65]=84; comp[84]=65; comp[67]=71; comp[71]=67;
    let mut buf=[0u8;16384]; let bases=[65u8,67,71,84];
    i=0; while i<n { buf[i as usize]=bases[(i&3) as usize]; i+=1; }
    let mut acc=0i64; let mut k=0i64;
    while k<r {
        let (mut lo,mut hi)=(0i64,n-1);
        while lo<hi {
            let a=comp[buf[lo as usize] as usize]; let b=comp[buf[hi as usize] as usize];
            buf[lo as usize]=b; buf[hi as usize]=a; lo+=1; hi-=1;
        }
        acc+=buf[0] as i64 + buf[(n-1) as usize] as i64; k+=1;
    } acc }
fn main(){ let r:i64=env::args().nth(1).unwrap_or("1".into()).parse().unwrap();
    std::process::exit((kernel(r)&1) as i32); }
