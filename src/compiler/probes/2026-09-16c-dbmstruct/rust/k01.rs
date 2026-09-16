fn two<'a,'b>(x:&'a i64,y:&'b i64)->i64 where 'a:'b{*x+*y}
fn f<'p,'q>(x:&'p i64,y:&'q i64)->i64{two(x,y)}
fn main(){let n=1i64;let m=2i64;std::process::exit(f(&n,&m) as i32);}
