fn make_adder(d: i32) -> impl Fn(i32) -> i32 { move |v| v + d }
fn main() { let f = make_adder(3); println!("{}", f(4)); }
