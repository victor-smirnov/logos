struct Stack<T> { items: Vec<T> }
impl<T: Copy> Stack<T> {
    fn peek(&self) -> Option<T> { match self.items.last() { Some(x) => Some(*x), None => None } }
    fn peek_or<E>(&self, e: E) -> Result<T, E> { match self.items.last() { Some(x) => Ok(*x), None => Err(e) } }
}
fn main() {
    let s = Stack { items: vec![1i64, 2, 3] };
    println!("{:?}", s.peek());
    println!("{:?}", s.peek_or(0i64));
}
