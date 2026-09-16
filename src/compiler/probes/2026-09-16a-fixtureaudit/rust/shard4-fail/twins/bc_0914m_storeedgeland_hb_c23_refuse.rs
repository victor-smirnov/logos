#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
struct Bag<T> { items: Vec<T> }
impl<T> Bag<T> {
    fn put(self: &mut Bag<T>, t: T) { self.items.push(t); }
    fn count(self: &Bag<T>) -> i64 { return self.items.len() as i64; }
}
fn lmain() -> i32 {
    let x: i64 = 5i64;
    let items0: Vec<&i64> = Vec::new();
    let mut bag: Bag<&i64> = Bag { items: items0 };
    bag.put(&x);
    {
        let d: i64 = 1i64;
        bag.put(&d);
    }
    return bag.count() as i32;
}
fn main() {}
