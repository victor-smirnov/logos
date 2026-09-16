fn two<'a,'b>(x:&'a mut i64,y:&'b mut i64)->i64 where 'a:'b{*x+*y}
fn f<'p,'q>(x:&'p mut i64,y:&'q mut i64)->i64{two(x,y)}
fn main(){let mut n=1i64;let mut m=2i64;std::process::exit(f(&mut n,&mut m) as i32);}
