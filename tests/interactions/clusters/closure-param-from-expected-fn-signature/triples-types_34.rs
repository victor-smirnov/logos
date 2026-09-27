fn compose<A, B, C, F: Fn(A) -> B, G: Fn(B) -> C>(f: F, g: G) -> impl Fn(A) -> C { move |x| g(f(x)) }
fn twice<T, F: Fn(T) -> T>(f: F) -> impl Fn(T) -> T { move |x| f(f(x)) }
fn apply_all<T: Copy, U, F: Fn(T) -> U>(xs: &[T], f: &F) -> Vec<U> { let mut out = Vec::new(); for x in xs { out.push(f(*x)); } return out; }
fn make_adder(n: i64) -> impl Fn(i64) -> i64 { move |x| x + n }
fn main() {
    let add3 = make_adder(3);
    let dbl_then_str = compose(|x: i64| x * 2, |y: i64| format!("<{}>", y));
    println!("{} {}", dbl_then_str(5), dbl_then_str(-1));
    let add6 = twice(add3);
    println!("{} {}", add6(1), add6(10));
    let quad = twice(twice(|x: i64| x + 1));
    println!("{}", quad(0));
    let pre = String::from("id");
    let tag = compose(move |x: i64| format!("{}{}", pre, x), |s: String| s.len());
    println!("{} {}", tag(7), tag(12345));
    let v = apply_all(&[1i64, 2, 3], &make_adder(100));
    println!("{:?}", v);
    let w = apply_all(&[1.5f64, 2.5], &|x: f64| (x * 2.0) as i64);
    println!("{:?}", w);
}
