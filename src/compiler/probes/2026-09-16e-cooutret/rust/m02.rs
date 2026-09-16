fn g<'a,'b>(x:&'a i64,y:&'b mut &'a i64)->i64 where 'a:'b{*y=x;*x}
fn f<'p,'q>(x:&'p i64,y:&'q mut &'p i64)->i64{g(x,y)}
fn main(){let n=3i64;let mut h:&i64=&n;std::process::exit(f(&n,&mut h) as i32);}
