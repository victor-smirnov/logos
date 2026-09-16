struct D { v: i64 }
fn read<'a>(src: &&'a D) -> i64 { src.v }
fn main() { let d = D { v: 6 }; let rd: &D = &d; let hold: &&D = &rd; let moved = d; let _ = read(hold) + moved.v; }
