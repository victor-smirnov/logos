// the PINNED-SHORT-SIDE twin: the bound's short side 'b reaches the RETURN type,
// so the caller can no longer shorten it and 'p: 'q is genuinely required.
fn needs<'a, 'b>(_x: &'a i32, y: &'b i32) -> &'b i32 where 'a: 'b { y }
fn caller<'p, 'q>(p: &'p i32, q: &'q i32) -> &'q i32 { needs(p, q) }
fn main() { let a: i32 = 1; let b: i32 = 2; std::process::exit(*caller(&a, &b)); }
