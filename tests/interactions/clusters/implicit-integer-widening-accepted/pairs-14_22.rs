fn pick<T: PartialOrd>(a: T, b: T) -> T { if a > b { a } else { b } }
fn main() { let a: i64 = 300; let b: u8 = 4; let x = pick(a, b); println!("{}", x); }
