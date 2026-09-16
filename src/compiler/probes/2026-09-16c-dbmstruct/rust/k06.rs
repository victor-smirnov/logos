fn longer<'a,'b>(x:&'a i64,y:&'b i64)->&'a i64 where 'b:'a{y}
fn f<'p>(x:&'p i64)->&'p i64{let tmp:i64=7;longer(x,&tmp)}
fn main(){let n=1i64;std::process::exit(*f(&n) as i32);}
