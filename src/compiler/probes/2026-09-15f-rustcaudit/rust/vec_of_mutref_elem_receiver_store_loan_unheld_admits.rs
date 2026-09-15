struct C { n: i64 }
impl C { fn bump(self: &mut C) { self.n = self.n + 1i64; } }
fn main() {
    let mut c: C = C { n: 1i64 };
    let mut inner: Vec<&C> = Vec::new();
    {
        let mut vs: Vec<&mut Vec<&C>> = Vec::new();
        vs.push(&mut inner);
        vs[0].push(&c);
    }
    c.bump();
    std::process::exit(inner.len() as i32);
}
