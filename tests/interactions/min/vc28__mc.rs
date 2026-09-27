fn main() {
    let names: Vec<String> = vec![String::from("abc")];
    let key = String::from("zzz");
    println!("{}", names.contains(&key));
}
