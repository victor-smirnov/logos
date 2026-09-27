struct W<T> { f: T }
impl<T: Copy + std::ops::Mul<Output = T>> W<T> {
    fn owned(self) -> impl Fn(T) -> T { let f = self.f; move |x| x * f }
}
fn main() {
    let g = W { f: 2i64 }.owned();
    let a: i64 = g(5);
    println!("{}", a);
    let v: Vec<i64> = (1i64..4).map(|x| g(x)).collect();
    println!("{:?}", v);
}
