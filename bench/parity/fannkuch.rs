use std::env;
#[inline(never)]
fn do_fannkuch(rot:i64)->i64 {
    let n=8i64;
    let mut perm=[0i32;8]; let mut tperm=[0i32;8]; let mut count=[0i32;8];
    for i in 0..n { perm[i as usize]=((i+rot)%n) as i32; }
    let mut checksum=0i64; let mut maxflips=0i64; let mut perm_count=0i64; let mut r=n;
    loop {
        while r!=1 { count[(r-1) as usize]=r as i32; r-=1; }
        for i in 0..n { tperm[i as usize]=perm[i as usize]; }
        let mut flips=0i64; let mut k=tperm[0] as i64;
        while k!=0 {
            let (mut lo,mut hi)=(0i64,k);
            while lo<hi { tperm.swap(lo as usize,hi as usize); lo+=1; hi-=1; }
            flips+=1; k=tperm[0] as i64;
        }
        if flips>maxflips { maxflips=flips; }
        if perm_count&1==0 { checksum+=flips; } else { checksum-=flips; }
        loop {
            if r==n { return checksum+maxflips; }
            let p0=perm[0];
            for i in 0..r { perm[i as usize]=perm[(i+1) as usize]; }
            perm[r as usize]=p0;
            count[r as usize]-=1;
            if count[r as usize]>0 { break; }
            r+=1;
        }
        perm_count+=1;
    }
}
#[inline(never)] fn kernel(reps:i64)->i64 { let mut acc=0i64; let mut k=0i64;
    while k<reps { acc^=do_fannkuch(k&7); k+=1; } acc }
fn main(){ let r:i64=env::args().nth(1).unwrap_or("1".into()).parse().unwrap();
    std::process::exit((kernel(r)&1) as i32); }
