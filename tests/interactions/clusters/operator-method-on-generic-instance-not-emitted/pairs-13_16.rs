#[derive(Clone, Copy, PartialEq, PartialOrd)]
struct Pt { x: i32, y: i32 }
fn maxp<T: PartialOrd + Copy>(a: T, b: T) -> T { if a > b { return a; } return b; }
fn main() {
    let m = maxp(Pt { x: 1, y: 5 }, Pt { x: 1, y: 3 });
    println!("{} {}", m.x, m.y);
    println!("{}", maxp(3, 9));
}
