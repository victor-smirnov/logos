struct D(&'static str);
enum E { Two(D, D), Empty }
fn peek(e: &E) -> &'static str {
    match e { E::Two(D(a), D(b)) if a.len() > b.len() => a, E::Two(_, D(b)) => b, E::Empty => "empty" }
}
fn main() { println!("{}", peek(&E::Two(D("long"), D("s")))); }
