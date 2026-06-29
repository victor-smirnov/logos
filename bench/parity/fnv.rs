use std::env;
#[inline(never)] fn fnv(buf: &[u8], seed: u64) -> u64 {
    let mut h: u64 = 14695981039346656037u64 ^ seed;
    for &b in buf { h = (h ^ (b as u64)).wrapping_mul(1099511628211u64); } h }
#[inline(never)] fn kernel(r: i64) -> i64 {
    let mut data = [0u8; 1024]; let mut i=0i64;
    while i<1024 { data[i as usize]=(i & 255) as u8; i+=1; }
    let mut acc=0u64; let mut k=0i64;
    while k<r { acc ^= fnv(&data, k as u64); k+=1; } acc as i64 }
fn main(){ let r:i64=env::args().nth(1).unwrap_or("1".into()).parse().unwrap();
    std::process::exit((kernel(r)&1) as i32); }
