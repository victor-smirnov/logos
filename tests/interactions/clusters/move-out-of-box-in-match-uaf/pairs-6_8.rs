enum Tree { Leaf(i32), Node(Box<Tree>, Box<Tree>) }
fn sum(t: &Tree) -> i32 { match t { Tree::Leaf(v) => *v, Tree::Node(a, b) => sum(a) + sum(b) } }
fn main() {
    let t = Tree::Node(Box::new(Tree::Node(Box::new(Tree::Leaf(1)), Box::new(Tree::Leaf(2)))), Box::new(Tree::Leaf(3)));
    let u = match t { Tree::Node(l, _) => *l, leaf => leaf };
    println!("{}", sum(&u));
}
