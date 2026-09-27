struct Wrapper<T>(T);
fn main() { let make = |x| Wrapper(x); let ww = make(7i16); println!("{}", ww.0); }
