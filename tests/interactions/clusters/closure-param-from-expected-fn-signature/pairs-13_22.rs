fn adder(n: i64) -> impl Fn(i64) -> i64 { return move |x| x + n; }
fn counter() -> impl FnMut() -> i64 { let mut c: i64 = 0; return move || { c += 1; c }; }
fn compose(f: impl Fn(i64) -> i64, g: impl Fn(i64) -> i64) -> impl Fn(i64) -> i64 { return move |x| g(f(x)); }
fn apply_n(mut f: impl FnMut() -> i64, n: i32) -> i64 { let mut last: i64 = 0; for _ in 0..n { last = f(); } return last; }
fn consume(f: impl FnOnce() -> String) -> usize { let s = f(); return s.len(); }
fn evens(limit: i64) -> impl Iterator<Item = i64> { return (0..limit).filter(|x| x % 2 == 0); }
fn main() {
    let a5 = adder(5);
    println!("{}", a5(10));
    let mut c = counter();
    c(); c();
    println!("{}", c());
    let h = compose(adder(1), |x| x * 3);
    println!("{}", h(4));
    let mut total: i64 = 0;
    let r = apply_n(|| { total += 2; total }, 4);
    println!("{} {}", r, total);
    let s = String::from("owned");
    let n = consume(move || { let mut t = s; t.push_str("!"); t });
    println!("{}", n);
    let k: i64 = 100;
    let by_ref = |x: i64| x + k;
    println!("{} {}", apply_n(counter(), 3), compose(by_ref, adder(k))(1));
    let v: Vec<i64> = evens(9).map(|x| x * x).collect();
    println!("{:?}", v);
}
