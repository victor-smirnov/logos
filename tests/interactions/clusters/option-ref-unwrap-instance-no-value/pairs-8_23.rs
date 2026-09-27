



fn main() {
    let v: Vec<u16> = vec![4u16, 9, 2];
    let m = v.iter().max();
    match m { Some(x) => println!("{}", *x), None => println!("none") }
}
