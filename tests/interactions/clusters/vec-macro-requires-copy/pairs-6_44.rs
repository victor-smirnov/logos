struct Pet { n: String }
fn main() {
    let v = vec![String::from("a"), String::from("b")];
    let p = vec![Pet { n: String::from("rex") }];
    println!("{} {}", v.len(), p[0].n);
}
