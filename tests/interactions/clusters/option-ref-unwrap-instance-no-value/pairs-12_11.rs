#[derive(Debug, PartialEq, PartialOrd, Eq, Ord)]
struct V { a: i32, b: i32 }
fn main() {
    let vs: Vec<V> = vec![V { a: 1, b: 5 }, V { a: 3, b: 0 }, V { a: 2, b: 9 }];
    println!("[{:?}]", vs.iter().max().unwrap());
}
