struct C { items: Vec<i64> }
fn main() {
    let c = C { items: vec![1, 2] };
    let r = &c;
    let k = move || r.items.len();
    println!("{} {}", k(), c.items.len());
}
