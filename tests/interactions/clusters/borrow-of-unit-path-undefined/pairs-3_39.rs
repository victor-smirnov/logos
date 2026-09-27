struct NF;
impl NF { fn k(&self) -> i64 { 3 } }
fn main() { let x = NF; println!("{}", x.k()); println!("{}", (&NF).k()); }
