enum Tree { Leaf(i64), Node(Box<Tree>, Box<Tree>) }
fn leftmost<'a>(mut t: &'a mut Tree) -> &'a mut i64 { loop { match t { Tree::Leaf(v) => return v, Tree::Node(l, _) => { t = l; } } } }
fn main() {
    let mut t = Tree::Node(Box::new(Tree::Leaf(3)), Box::new(Tree::Leaf(4)));
    *leftmost(&mut t) = 100;
    if let Tree::Node(l, _) = &t { if let Tree::Leaf(v) = &**l { println!("{}", v); } }
}
