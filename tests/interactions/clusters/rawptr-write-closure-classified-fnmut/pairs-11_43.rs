fn main() { let mut s = String::from("ab"); let ps: *mut String = &mut s; let push = |c: char| unsafe { (*ps).push(c) }; push('c'); push('d'); println!("{}", s); }
