struct Item { name: String }
fn dup(x: &Item) -> Item { Item { name: x.name.clone() } }
fn main() { let a = Item { name: String::from("a") }; let first = dup(&a); let owned = a; println!("{} {}", first.name.len(), owned.name.len()); }
