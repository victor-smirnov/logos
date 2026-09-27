trait Node { type Val; const DEPTH_LIMIT: i64; fn val(&self) -> Self::Val; fn children(&self) -> &Vec<Self> where Self: Sized; }
struct T { v: i64, kids: Vec<T> }
impl Node for T { type Val = i64; const DEPTH_LIMIT: i64 = 3; fn val(&self) -> i64 { return self.v; } fn children(&self) -> &Vec<T> { return &self.kids; } }
fn total<N: Node<Val = i64>>(n: &N, depth: i64) -> i64 { if depth >= N::DEPTH_LIMIT { return 0; } let mut s = n.val(); for c in n.children().iter() { s += total(c, depth + 1); } return s; }
trait Peano { const N: i64; }
struct Z; struct S<P>(P);
impl Peano for Z { const N: i64 = 0; }
impl<P: Peano> Peano for S<P> { const N: i64 = P::N + 1; }
fn leaf(v: i64) -> T { return T { v: v, kids: Vec::new() }; }
fn main() {
    let t = T { v: 1, kids: vec![T { v: 2, kids: vec![leaf(3), T { v: 4, kids: vec![leaf(100)] }] }, leaf(5)] };
    println!("{}", total(&t, 0));
    println!("{}", total(&t, 1));
    println!("{} {}", <S<S<S<Z>>> as Peano>::N, S::<Z>::N);
}
