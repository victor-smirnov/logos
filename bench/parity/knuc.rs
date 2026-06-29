use std::env;
use std::collections::HashMap;
#[inline(never)] fn kernel(r:i64)->i64 {
    let n=8192i64; let k=6i64; let mask=(1i64<<(2*k))-1;
    let mut seq=[0i64;8192]; let mut acc=0i64; let mut rep=0i64;
    while rep<r {
        let mut i=0i64; while i<n { seq[i as usize]=(i*1103515245+12345+rep)&3; i+=1; }
        let mut m:HashMap<i64,i64>=HashMap::new();
        let mut code=0i64; i=0; let mut best=0i64;
        while i<n {
            code=((code<<2)|seq[i as usize])&mask;
            if i>=k-1 { let e=m.entry(code).or_insert(0); *e+=1; if *e>best { best=*e; } }
            i+=1;
        }
        acc+=best;
        rep+=1;
    } acc }
fn main(){ let r:i64=env::args().nth(1).unwrap_or("1".into()).parse().unwrap();
    std::process::exit((kernel(r)&1) as i32); }
