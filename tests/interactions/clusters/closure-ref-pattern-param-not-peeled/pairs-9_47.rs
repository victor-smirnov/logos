#[derive(Clone, Copy)]
struct V { x: i64 }
fn main() { let v = vec![V { x: 1 }, V { x: 5 }]; let b = v.iter().fold(V { x: 0 }, |best, &p| if p.x > best.x { p } else { best }); println!("{}", b.x); }
