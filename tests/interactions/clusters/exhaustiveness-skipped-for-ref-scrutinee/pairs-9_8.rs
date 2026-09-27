enum E { A(i32), C }
fn main() { let e = E::C; let r = &e; let v = match r { E::A(x) => *x }; println!("{}", v); }
