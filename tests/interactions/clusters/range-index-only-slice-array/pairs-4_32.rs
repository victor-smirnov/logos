fn bump(xs: &mut [i64; 4]) { let r = &mut xs[1..3]; r[0] *= 10; }
fn first(xs: &mut [i64]) { xs[0] = 7; }
fn main() {
    let mut arr = [3i64, 9, 2, 7];
    bump(&mut arr);
    first(&mut arr[2..]);
    let s: &[i64] = &arr[..2];
    println!("{:?} {:?}", arr, s);
}
