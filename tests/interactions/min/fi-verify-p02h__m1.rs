struct A { c: i64 }
struct B { c: i64 }
impl From<A> for B { fn from(a: A) -> B { B { c: a.c } } }
fn low() -> Result<i64, A> { Err(A { c: 3 }) }
fn lift<E: From<A>>() -> Result<i64, E> { let x = low()?; Ok(x) }
fn main() { let r: Result<i64, B> = lift(); match r { Ok(v) => println!("ok {}", v), Err(b) => println!("err {}", b.c) } }
