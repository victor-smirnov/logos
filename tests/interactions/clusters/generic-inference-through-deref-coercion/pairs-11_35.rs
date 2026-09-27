enum L<T> { C(T, Box<L<T>>), N }
fn cnt<T>(l: &L<T>) -> i32 { match l { L::N => 0, L::C(_, t) => 1 + cnt(t) } }
fn main() { let l: L<i32> = L::C(1, Box::new(L::C(2, Box::new(L::N)))); println!("{}", cnt(&l)); }
