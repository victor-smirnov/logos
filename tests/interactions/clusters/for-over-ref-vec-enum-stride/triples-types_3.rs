#[derive(Clone, Copy)]
enum L { R, G(i64) }
fn w(l: &L) -> i64 { match l { L::R => 60, L::G(t) => *t } }
fn main() {
    let c: Vec<L> = vec![L::R, L::G(30), L::G(3)];
    for l in &c { print!("{} ", w(l)); }
    println!();
    for l in c.iter() { print!("{} ", w(l)); }
    println!();
}
