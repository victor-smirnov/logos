fn main() {
    let g = String::from("hello");
    let count = loop { let s = g.clone(); if s.len() > 3 { break s.len() * 2; } };
    println!("{}", count);
}
