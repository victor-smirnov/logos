struct Holder<'a, T> { items: &'a [T] }
impl<'a, T: PartialOrd + Copy> Holder<'a, T> {
    fn max_ref(&self) -> &'a T {
        let mut best = &self.items[0];
        for x in self.items.iter() { if *x > *best { best = x; } }
        best
    }
}
fn main() {
    let data = [3i32, 9, 2];
    let h = Holder { items: &data[..] };
    let m = h.max_ref();
    println!("{}", m);
}
