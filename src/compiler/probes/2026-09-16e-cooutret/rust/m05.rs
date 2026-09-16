fn two<'a,'b>(x:&'a i64,y:&'b i64)->(&'a i64,i64) where 'a:'b{(x,*y)}
fn f<'p,'q>(x:&'p i64,y:&'q i64)->(&'p i64,i64){two(x,y)}
fn main(){let n=8i64;let m=1i64;let t=f(&n,&m);std::process::exit((*t.0+t.1) as i32);}
