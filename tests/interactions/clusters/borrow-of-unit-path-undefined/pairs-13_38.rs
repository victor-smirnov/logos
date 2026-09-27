struct Bag;
impl Bag { fn v(&self) -> i32 { return 7; } }
fn take(i: usize) -> usize { return i + 1; }
fn by_ref(b: &Bag) -> i32 { return b.v(); }
fn main() {
    let mut s: usize = 0;
    for i in 0usize..3usize { s += take(i); }
    println!("{}", s);
    println!("{}", by_ref(&Bag));
}
