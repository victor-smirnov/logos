use std::fmt::Display;
fn main() {
    let k = 42i64; let j = 7i64;
    let items: [&dyn Display; 2] = [&k, &j];
    let s: &[&dyn Display] = &items;
    let d: &dyn Display = &k;
    println!("b {}", d);
    let dd: &&dyn Display = &d;
    println!("c {}", dd);
}
