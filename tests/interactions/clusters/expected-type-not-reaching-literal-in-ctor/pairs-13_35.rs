enum P<T> { E, F(T) }
struct Bag { items: [P<i64>; 2] }
fn main() {
    let b = Bag { items: [P::F(4), P::E] };
    let o: Option<i64> = Some(5);
    let arr: [Option<i64>; 2] = [Some(1), None];
    match &b.items[0] { P::F(v) => println!("{} {:?} {:?}", v, o, arr), P::E => println!("e") }
}
