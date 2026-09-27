fn b1(xs: &mut [i64; 4]) { for x in xs.iter_mut() { *x += 1; } xs[1] *= 10; }
fn b2(xs: &mut [i64]) { for x in xs.iter_mut() { *x += 1; } xs[1] *= 10; }
fn b3(xs: &mut Vec<i64>) { for x in xs.iter_mut() { *x += 1; } xs[1] *= 10; }
fn main() {
    let mut arr = [3i64, 9, 2, 7]; b1(&mut arr); b2(&mut arr);
    let mut v = vec![1i64, 2]; b3(&mut v);
    println!("{:?} {:?}", arr, v);
}
