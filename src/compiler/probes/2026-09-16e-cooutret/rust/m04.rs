struct H<'a>{r:&'a i64}
fn two<'a,'b>(x:&'a i64,y:&'b i64)->H<'b> where 'a:'b{H{r:y}}
fn f<'p,'q>(x:&'p i64,y:&'q i64)->H<'q>{two(x,y)}
fn main(){let n=6i64;let m=7i64;std::process::exit(*f(&n,&m).r as i32);}
