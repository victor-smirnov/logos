fn main() {
    let f = |v: i32| -> i32 { match v { 0 => -1, x => x * 2 } };
    println!("{} {}", f(0), f(4));
}
