struct Wrap<T> { inner: T }
impl<T: PartialEq> PartialEq for Wrap<T> { fn eq(&self, o: &Wrap<T>) -> bool { return self.inner == o.inner; } }
fn main() {
    let a = Wrap { inner: String::from("s") };
    let b = Wrap { inner: String::from("s") };
    let c = a == b;
    let d = a == b;
}
