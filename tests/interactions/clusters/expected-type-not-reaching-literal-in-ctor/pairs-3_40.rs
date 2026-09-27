struct V2<T> { x: T, y: T }
fn f(v: V2<i64>) -> i64 { v.x + v.y }
fn main() { println!("{}", f(V2 { x: 3, y: 4 })); let w: V2<i64> = V2 { x: 5, y: 6 }; println!("{}", w.x); }
