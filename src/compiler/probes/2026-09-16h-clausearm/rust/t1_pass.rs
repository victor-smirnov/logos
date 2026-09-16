// twin of the SPEC fixture program (region_diag_1__callee-bound-at-call), verbatim shape
fn needs<'a, 'b>(_x: &'a i32, _y: &'b i32) -> i32 where 'a: 'b { 0i32 }
fn caller<'p, 'q>(p: &'p i32, q: &'q i32) -> i32 { needs(p, q) }
fn main() {
    let a: i32 = 1; let b: i32 = 2;
    std::process::exit(caller(&a, &b));
}
