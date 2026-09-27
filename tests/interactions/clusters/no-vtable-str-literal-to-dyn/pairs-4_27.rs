use std::fmt::Display;
struct P { x: i64 }
impl Display for P { fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result { write!(f, "P{}", self.x) } }
fn show_all(items: &[&dyn Display]) -> String { let mut s = String::new(); for it in items { s.push_str(&format!("[{}]", it)); } s }
fn boxed(n: i64) -> Box<dyn Display> { if n > 0 { Box::new(P { x: n }) } else { Box::new(String::from("neg")) } }
fn main() {
    let p = P { x: 3 };
    let name = String::from("bob");
    let k = 42i64;
    let items: [&dyn Display; 3] = [&p, &name, &k];
    println!("{}", show_all(&items));
    let b1 = boxed(5); let b2 = boxed(-1);
    println!("{} {}", b1, b2);
    let joined = format!("{}+{}", b1, b2);
    println!("{} {}", joined, joined.len());
}
