use std::env;
#[inline(never)]
fn escape(cr:f64,ci:f64,maxit:i64)->i64 {
    let (mut zr,mut zi)=(0.0,0.0); let mut it=0i64;
    while it<maxit {
        let (zr2,zi2)=(zr*zr,zi*zi);
        if zr2+zi2>4.0 { return it; }
        let nzr=zr2-zi2+cr; zi=2.0*zr*zi+ci; zr=nzr; it+=1;
    } maxit }
#[inline(never)]
fn kernel(r:i64)->i64 {
    let n=96i64; let maxit=100i64; let mut acc=0i64; let mut k=0i64;
    while k<r {
        let off=(k&7) as f64 * 0.0001;
        let mut sum=0i64; let mut py=0i64;
        while py<n {
            let ci=(py as f64)*(2.0/(n as f64))-1.0+off; let mut px=0i64;
            while px<n {
                let cr=(px as f64)*(2.5/(n as f64))-2.0+off;
                sum+=escape(cr,ci,maxit); px+=1;
            } py+=1;
        } acc^=sum; k+=1;
    } acc }
fn main(){ let r:i64=env::args().nth(1).unwrap_or("1".into()).parse().unwrap();
    std::process::exit((kernel(r)&1) as i32); }
