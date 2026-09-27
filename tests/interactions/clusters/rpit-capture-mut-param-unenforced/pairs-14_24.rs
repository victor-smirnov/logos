trait Named { fn name(&self) -> &str; }
struct P { n: String }
impl Named for P { fn name(&self) -> &str { &self.n } }
fn longest<'a>(a: &'a impl Named, b: &'a impl Named) -> &'a str { if a.name().len() >= b.name().len() { a.name() } else { b.name() } }
fn evens<'a>(v: &'a Vec<i64>) -> impl Iterator<Item = &'a i64> + 'a { v.iter().filter(|x| **x % 2 == 0) }
fn bump_all(v: &mut Vec<i64>) -> impl Fn(i64) -> i64 { let n = v.len() as i64; for x in v.iter_mut() { *x += n; } move |y| y + n }
fn first_word(s: &str) -> impl Named + '_ { W { s: s.split(' ').next().unwrap() } }
struct W<'a> { s: &'a str }
impl<'a> Named for W<'a> { fn name(&self) -> &str { self.s } }
fn main() {
    let a = P { n: String::from("alice") }; let b = P { n: String::from("bo") };
    println!("{}", longest(&a, &b));
    let mut v = vec![1i64, 2, 3, 4];
    let f = bump_all(&mut v);
    println!("{:?} {}", v, f(10));
    let e: Vec<&i64> = evens(&v).collect();
    println!("{:?}", e);
    let text = String::from("hello big world");
    let w = first_word(&text);
    println!("{}", w.name());
}
