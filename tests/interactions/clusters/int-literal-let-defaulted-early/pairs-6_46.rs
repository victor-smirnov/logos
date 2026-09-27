fn count(xs: &[u8]) -> u64 { let mut c = 0; for x in xs { if *x > 1 { c += 1; } } return c; }
fn pick(f: bool) -> u16 { let a = 7; let b = 9; if f { a } else { b } }
fn main() { println!("{} {}", count(&[1, 2, 3]), pick(false)); }
