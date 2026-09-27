

#[derive(Clone, Copy)] struct P { a: i64, b: i64 }
struct C { p: P }
fn mk(a: i64) -> P { P { a: a, b: 2 } }
fn main() {
    let f = |q: P| q.a * 10 + q.b;
    let c = C { p: P { a: 1, b: 2 } };
    println!("{}", f(c.p));
    println!("{}", f(mk(3)));
}
