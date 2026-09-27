fn main() {
    let names: Vec<String> = vec![String::from("b"), String::from("a")];
    let key = String::from("a");
    println!("{}", names.contains(&key));
    println!("{}", names[1]);
}
