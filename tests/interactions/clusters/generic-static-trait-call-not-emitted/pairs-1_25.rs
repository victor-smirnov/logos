trait Zero { fn zero() -> Self; }
impl Zero for String { fn zero() -> String { String::from("z") } }
fn mk<M: Zero>() -> M { M::zero() }
fn main() { let s: String = mk(); println!("{}", s); }
