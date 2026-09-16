fn a<'a,'b,'c>(x:&mut &'a i64,y:&mut &'b i64,z:&mut &'c i64)->i32 where 'b:'a+'c{*x=*y;*z=*y;0}
fn c<'a,'b,'c>(x:&mut &'a i64,y:&mut &'b i64,z:&mut &'c i64)->i32{a(x,y,z)}
fn main(){}
