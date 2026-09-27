enum K { A, B, C }
fn k(x: &K) -> i64 { match x { K::A => 1, K::B => 2 } }
fn main() { let c = K::C; println!("{}", k(&c)); let r = &c; let v = match r { K::A => 10, K::B => 20 }; println!("{}", v); }
