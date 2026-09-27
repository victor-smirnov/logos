fn compose<A, B, C>(f: impl Fn(A) -> B, g: impl Fn(B) -> C) -> impl Fn(A) -> C { move |x| g(f(x)) }
fn main() { let h = compose(|x: i32| x + 1, |y| y * 2); println!("{}", h(5)); let k = compose(|x: i32| x + 1, |y: i32| y * 2); println!("{}", k(5)); let r = k(6); println!("{}", r); }
