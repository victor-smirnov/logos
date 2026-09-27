#[derive(Clone,Copy)] struct Pair { a: i32, b: i32 }
fn main() {
    let p = Some(Pair { a: 5, b: 9 });
    let Some(Pair { a, b }) = p else { return; };
    println!("{} {}", a, b);
    let q = Some((1, 2));
    let Some((x, y)) = q else { return; };
    println!("{} {}", x, y);
}
