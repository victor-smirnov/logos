struct D { v: i64 }
fn read<'a>(src: &&'a D) -> i64 { src.v }
fn main() { let d = D { v: 6 }; let rd: &D = &d; std::process::exit((read(&rd) - 6) as i32); }
