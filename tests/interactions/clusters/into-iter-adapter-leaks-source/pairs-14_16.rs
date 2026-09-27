fn main() { let v: Vec<String> = vec![String::from("a"), String::from("bb"), String::from("ccc")]; let w: Vec<String> = v.into_iter().filter(|s| s.len() > 1).collect(); println!("{}", w.len()); }
