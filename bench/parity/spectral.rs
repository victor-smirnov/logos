use std::env;
fn eval_a(i:i64,j:i64)->f64 { let s=i+j; let d=s*(s+1)/2+i+1; 1.0/(d as f64) }
#[inline(never)]
fn a_times_u(u:&[f64;256], v:&mut[f64;256], n:i64){
    let mut i=0i64; while i<n { let mut sum=0.0; let mut j=0i64;
        while j<n { sum+=eval_a(i,j)*u[j as usize]; j+=1; } v[i as usize]=sum; i+=1; } }
#[inline(never)]
fn at_times_u(u:&[f64;256], v:&mut[f64;256], n:i64){
    let mut i=0i64; while i<n { let mut sum=0.0; let mut j=0i64;
        while j<n { sum+=eval_a(j,i)*u[j as usize]; j+=1; } v[i as usize]=sum; i+=1; } }
#[inline(never)]
fn kernel(r:i64)->i64 {
    let n=256i64; let mut u=[1.0f64;256]; let mut t=[0.0f64;256]; let mut v=[0.0f64;256];
    let mut acc=0i64; let mut k=0i64;
    while k<r {
        a_times_u(&u,&mut t,n); at_times_u(&t,&mut v,n);
        let mut i=0i64; while i<n { u[i as usize]=v[i as usize]+((k&1)as f64)*0.0; i+=1; }
        acc^=(v[0]*1000000.0) as i64; k+=1;
    } acc }
fn main(){ let r:i64=env::args().nth(1).unwrap_or("1".into()).parse().unwrap();
    std::process::exit((kernel(r)&1) as i32); }
