struct Pool<T> { items: Vec<Box<T>> }
impl<T> Pool<T> { fn new() -> Pool<T> { Pool { items: Vec::new() } } }
fn main() { let mut p: Pool<u8> = Pool::new(); p.items.push(Box::new(3)); println!("{}", p.items.len()); }
