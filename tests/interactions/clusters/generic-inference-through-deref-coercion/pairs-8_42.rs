enum List<T> { Nil, Cons(T, Box<List<T>>) }
fn len<T>(l: &List<T>) -> i64 { match l { List::Nil => 0, List::Cons(_, t) => 1 + len(t) } }
fn main() { let l: List<i64> = List::Cons(1, Box::new(List::Cons(2, Box::new(List::Nil)))); println!("{}", len(&l)); }
