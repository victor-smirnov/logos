fn main() { let o = Some(String::from("abc")); let c = o.and_then(|s| if s.len() > 2 { Some(s) } else { None }); println!("{:?}", c); }
