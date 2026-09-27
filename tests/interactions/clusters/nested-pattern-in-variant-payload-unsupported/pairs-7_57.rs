#[derive(Clone, Copy)]
struct Pos { x: i32, y: i32 }
enum Sh { Line { from: Pos, to: Pos } }
fn main() {
    let s = Sh::Line { from: Pos { x: 1, y: 2 }, to: Pos { x: 3, y: 4 } };
    let o = Some(&s);
    if let Some(Sh::Line { from: Pos { x: fx, .. }, .. }) = o { println!("fx={}", fx); }
    match s { Sh::Line { to: Pos { y: ty, .. }, .. } => println!("ty={}", ty) }
}
