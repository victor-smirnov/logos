

enum List<T> { Cons(T, Box<List<T>>), Nil }
impl<T: Copy> List<T> {
    fn len(&self) -> usize { match self { List::Cons(_, t) => 1 + (**t).len(), List::Nil => 0 } }
    fn nm(&self) -> usize { match self { List::Cons(_, t) => 1 + t.nm(), List::Nil => 0 } }
}
fn main() {
    let l: List<i64> = List::Cons(1, Box::new(List::Nil));
    println!("{}", l.len());
    let c: List<char> = List::Cons('a', Box::new(List::Nil));
    println!("{}", c.nm());
}
