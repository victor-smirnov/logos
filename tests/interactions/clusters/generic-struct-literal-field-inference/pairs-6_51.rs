struct H<'a, T> { items: &'a [T] }
fn main() {
    let data = [3i32, 9];
    let h = H { items: &data };
    println!("{}", h.items[1]);
}
