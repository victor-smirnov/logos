use std::ops::Add;
enum Expr { Num(i64), Plus(Box<Expr>, Box<Expr>) }
impl Add for Expr { type Output = Expr; fn add(self, o: Expr) -> Expr { return Expr::Plus(Box::new(self), Box::new(o)); } }
fn eval(e: &Expr) -> i64 { match e { Expr::Num(n) => *n, Expr::Plus(a, b) => eval(a) + eval(b) } }
fn main() {
    let e = Expr::Num(2) + Expr::Num(3);
    println!("{}", eval(&e));
}
