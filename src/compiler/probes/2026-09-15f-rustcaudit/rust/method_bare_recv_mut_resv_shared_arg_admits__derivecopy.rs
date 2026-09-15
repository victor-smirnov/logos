#[derive(Clone, Copy)]
struct S { a: i64 }
impl S {
    fn bump(self: &mut Self, o: &S) -> i64 {
        self.a = self.a + o.a;
        return self.a;
    }
}
fn main() {
    let mut s: S = S { a: 1i64 };
    let n: i64 = s.bump(&s);
    if n != 2i64 { std::process::exit(1); }
    std::process::exit(0);
}
