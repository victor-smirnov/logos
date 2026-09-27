fn main() { let s = "a  b c"; let n = s.split(' ').filter(|x| x.len() > 0).count(); println!("{}", n); }
