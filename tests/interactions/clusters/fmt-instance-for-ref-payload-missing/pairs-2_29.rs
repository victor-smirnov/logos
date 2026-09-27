fn main() { let v: Vec<String> = vec![String::from("a")]; let o: Option<&String> = v.last(); println!("{:?}", o); }
