fn s<const N: usize>(a: &[i64; N]) -> i64 { let r: i64 = a.iter().sum(); r }
fn l<const N: usize>(a: &[i64; N]) -> usize { a.iter().count() }
fn main() {
    let a = [1i64, 2, 3];
    println!("{} {}", s(&a), l(&a));
    let b = [[1u8, 2, 3], [4, 5, 6]];
    let c = [[1i64, 2, 3], [4, 5, 6]];
    println!("{} {} {}", b[1][2], c[1][2], b[0][1]);
}
