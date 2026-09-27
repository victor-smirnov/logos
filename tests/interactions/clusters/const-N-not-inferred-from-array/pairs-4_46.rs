struct Buf<const N: usize> { data: [i64; N] }
impl<const N: usize> Buf<N> { fn len(&self) -> usize { N } }
fn main() {
    let b = Buf { data: [1, 1, 1] };
    println!("{}", b.len());
    let c = [4i64, 5].into_iter().map(|x| x + 1).sum::<i64>();
    println!("{}", c);
}
