struct Holder<'a> { items: &'a [i32] }
impl<'a> Holder<'a> {
    fn first(&self) -> &'a i32 { &self.items[0] }
}
fn main() {
    let data = [3, 9];
    let m: &i32;
    {
        let h = Holder { items: &data };
        m = h.first();
    }
    println!("{}", m);
}
