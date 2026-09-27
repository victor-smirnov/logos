fn main() {
    let ws = ["a", "abcd", "ab"];
    let s: &[&str] = &ws;
    match s { [x, y, z] => println!("{} {} {}", x, y, z), _ => println!("other") }
    match s { [_, rest @ ..] => println!("{}", rest.len()), _ => println!("other") }
    match s { [.., last] => println!("{}", last), _ => println!("other") }
}
