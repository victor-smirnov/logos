fn esc<'a,'b>(x:&'a i64,out:&'b mut &'a i64) where 'a:'b{*out=x;}
fn f<'p,'q>(x:&'p i64,out:&'q mut &'p i64){esc(x,out);}
fn main(){let n=1i64;let mut r:&i64=&n;{let mut h:&i64=&n;f(&n,&mut h);r=h;}std::process::exit(*r as i32);}
