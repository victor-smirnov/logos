struct W<T, U> { a: T, tag: U, n: i64 }
fn main() {
    let w = W { a: 5i32, tag: 8u8, n: 1 };
    let w2 = W { n: 9, ..w };
    println!("{} {} {}", w2.a, w2.tag, w2.n);
}
