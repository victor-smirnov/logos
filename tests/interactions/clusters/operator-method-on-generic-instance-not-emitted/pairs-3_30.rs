use std::cmp::Ordering;
#[derive(Clone, Copy)]
struct M { v: i64, p: i64 }
impl PartialEq for M { fn eq(&self, o: &M) -> bool { self.v == o.v } }
impl PartialOrd for M { fn partial_cmp(&self, o: &M) -> Option<Ordering> { if self.v < o.v { Some(Ordering::Less) } else if self.v > o.v { Some(Ordering::Greater) } else { Some(Ordering::Equal) } } }
fn bigger<T: PartialOrd>(a: T, b: T) -> bool { a > b }
fn main() {
    println!("{}", bigger(M { v: 3, p: 0 }, M { v: 2, p: 0 }));
    println!("{}", M { v: 3, p: 0 } > M { v: 2, p: 0 });
}
