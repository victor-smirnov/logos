fn a<'a,'b>(x:&mut &'a i64,y:&mut &'b i64) where 'b:'a{*x=*y;}
fn c<'a,'b>(x:&mut &'a i64,y:&mut &'b i64){a(x,y);}
fn main(){}
