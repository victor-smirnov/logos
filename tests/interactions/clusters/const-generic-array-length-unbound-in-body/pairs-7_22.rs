enum Bin<const N: usize> { Empty, Full([i64; N]) }
impl<const N: usize> Bin<N> {
    fn total(&self) -> i64 { match self { Bin::Empty => -1, Bin::Full(a) => { let mut s = 0i64; for x in a.iter() { s += *x; } return s; } } }
}
fn main() {
    let c: Bin<3> = Bin::Full([1i64, 2, 3]);
    let d: Bin<2> = Bin::Empty;
    println!("{} {}", c.total(), d.total());
    if let Bin::Full(arr) = &c { println!("{}", arr[1]); }
}
