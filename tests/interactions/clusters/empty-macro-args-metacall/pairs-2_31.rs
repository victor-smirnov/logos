fn main() {
    let o: Option<i64> = Some(3);
    let Some(x) = o else { panic!() };
    println!("{}", x);
}
