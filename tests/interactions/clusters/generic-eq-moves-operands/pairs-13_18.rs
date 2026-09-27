
struct Wrap<T> { inner: T, tag: i32 }
impl<T: PartialEq> PartialEq for Wrap<T> { fn eq(&self, o: &Wrap<T>) -> bool { return self.inner == o.inner && self.tag == o.tag; } }
fn main() {
    let ws = Wrap { inner: String::from("s"), tag: 1 };
    let ws2 = Wrap { inner: String::from("s"), tag: 1 };
    println!("{}", ws == ws2);
    println!("{}", ws.inner);
}
