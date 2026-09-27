enum E { B(i32, char, i32) }
fn f(e: &E) -> i32 { match e { E::B(a, op @ ('+' | '*'), b) => match op { '+' => a + b, _ => a * b }, E::B(_, _, _) => 0 } }
fn main() { println!("{} {} {}", f(&E::B(2, '+', 3)), f(&E::B(2, '*', 3)), f(&E::B(1, '/', 1))); }
