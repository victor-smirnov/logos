enum Node { Leaf(i64), Link(i64) }
fn f(p: *mut Node) -> i64 { unsafe { match &*p { Node::Leaf(v) => *v, Node::Link(q) => *q + 1 } } }
fn g(p: *mut Node) -> i64 { unsafe { match &mut *p { Node::Leaf(v) => *v, Node::Link(q) => *q + 1 } } }
fn main() { let mut leaf = Node::Link(5); let q: *mut Node = &mut leaf; println!("{}", g(q)); println!("{}", f(q)); }
