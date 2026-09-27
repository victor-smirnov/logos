use std::fmt::Display;
fn main() { let d: &dyn Display = &"lit"; println!("{}", d); }
