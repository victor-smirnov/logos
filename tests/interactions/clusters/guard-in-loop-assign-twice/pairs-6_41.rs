enum A { P(String), R(i64) }
fn f(i: i64) -> Result<i64, A> { if i == 0 { Ok(1) } else if i == 1 { Err(A::P(String::from("p"))) } else { Err(A::R(i)) } }
fn main() { for i in 0..3 { match f(i) { Ok(t) => println!("ok {}", t), Err(A::P(m)) => println!("p {}", m), Err(A::R(v)) => println!("r {}", v) } } }
