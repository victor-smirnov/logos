enum Sh { Line { a: i32, b: i32 }, Dot(i32) }
fn main() {
    let items: Vec<Sh> = vec![Sh::Line { a: 3, b: 2 }, Sh::Dot(1)];
    if let [Sh::Dot(d), ..] = items.as_slice() { println!("wrong d={}", d); } else { println!("not dot"); }
}
