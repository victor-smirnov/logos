


trait Expr { fn eval(&self) -> i64; }
struct Num(i64);
struct Add { l: Box<dyn Expr>, r: Box<dyn Expr> }
impl Expr for Num { fn eval(&self) -> i64 { self.0 } }
impl Expr for Add { fn eval(&self) -> i64 { self.l.eval() + self.r.eval() } }
fn main() {
    let a = Add { l: Box::new(Num(1)), r: Box::new(Num(2)) };
    println!("{}", a.eval());
    let b: Box<dyn Expr> = Box::new(Add { l: Box::new(Num(3)), r: Box::new(a) });
    println!("{}", b.eval());
}
