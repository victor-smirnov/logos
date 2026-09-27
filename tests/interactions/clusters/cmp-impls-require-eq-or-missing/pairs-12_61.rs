struct N { s: String }
enum E { A(String), B }
#[derive(PartialEq)]
struct F { x: f64 }
fn main() {
    let v = vec![String::from("a"), String::from("b")];
    let w = vec![N { s: String::from("x") }];
    let u = vec![E::A(String::from("q")), E::B];
    let f = vec![F { x: 1.5 }];
    println!("{} {} {} {}", v.len(), w.len(), u.len(), f.contains(&F { x: 1.5 }));
}
