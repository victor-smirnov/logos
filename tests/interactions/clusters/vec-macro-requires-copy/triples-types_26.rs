struct Bag { xs: Vec<i64> }
struct Sq(i64);
fn main() {
    let v = vec![Sq(2), Sq(3)];
    let bags = vec![Bag { xs: vec![2, 9] }, Bag { xs: vec![4] }];
    println!("{} {}", v[1].0, bags[0].xs[1]);
}
