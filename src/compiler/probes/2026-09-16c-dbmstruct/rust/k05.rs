struct H<'a>{r:&'a i64}
fn two<'a,'b>(x:H<'a>,y:H<'b>)->i64 where 'a:'b{*x.r+*y.r}
fn f<'p,'q>(x:H<'p>,y:H<'q>)->i64{two(x,y)}
fn main(){let n=1i64;let m=2i64;std::process::exit(f(H{r:&n},H{r:&m}) as i32);}
