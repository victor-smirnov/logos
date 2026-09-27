trait Maker { type Out; }
struct SM { pre: String }
impl Maker for SM { type Out = String; }
struct Pair<M: Maker> { m: M, a: M::Out, b: M::Out }
fn split<M: Maker>(p: Pair<M>) -> M::Out { let a = p.a; return a; }
fn main() {
    let o = split(Pair { m: SM { pre: String::from("m") }, a: String::from("aa"), b: String::from("bb") });
    println!("{}", o);
}
