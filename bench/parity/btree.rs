use std::env;
enum Tree { Leaf, Node(Box<Tree>, Box<Tree>) }
fn build(depth:i64)->Tree {
    if depth<=0 { return Tree::Leaf; }
    Tree::Node(Box::new(build(depth-1)), Box::new(build(depth-1)))
}
fn check(t:&Tree)->i64 {
    match t { Tree::Leaf=>1, Tree::Node(l,r)=>1+check(l)+check(r) }
}
#[inline(never)] fn kernel(reps:i64)->i64 {
    let depth=12i64; let mut acc=0i64; let mut k=0i64;
    while k<reps { let t=build(depth); acc^=check(&t); k+=1; } acc }
fn main(){ let r:i64=env::args().nth(1).unwrap_or("1".into()).parse().unwrap();
    std::process::exit((kernel(r)&1) as i32); }
