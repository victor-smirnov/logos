fn main() { let v = [1, 2, 3]; let mut it = v.iter(); let Some(&x) = it.next() else { return; }; println!("{}", x); }
