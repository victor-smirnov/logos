enum Tok { Num(i64), Op(char) }
fn eval(a: &Tok) -> i64 {
    match a {
        Tok::Op(o @ ('+' | '-')) => if *o == '+' { 1 } else { 4 },
        Tok::Op(_) => 2,
        Tok::Num(_) => 3,
    }
}
fn main() { println!("{} {} {}", eval(&Tok::Op('+')), eval(&Tok::Op('-')), eval(&Tok::Num(1))); }
