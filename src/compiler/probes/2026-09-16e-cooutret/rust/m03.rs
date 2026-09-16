fn two<'a,'b>(x:&'a i64,y:&'b i64)->&'a i64 where 'a:'b{x}
fn f<'p,'q>(x:&'p i64,y:&'q i64)->&'p i64{two(x,y)}
fn main(){let n=4i64;let m=5i64;std::process::exit(*f(&n,&m) as i32);}
