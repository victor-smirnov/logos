struct Item { name: String, qty: u32 }
impl Clone for Item { fn clone(&self) -> Item { Item { name: self.name.clone(), qty: self.qty } } }
fn main() { let items: Vec<Item> = vec![Item { name: String::from("a"), qty: 1 }]; let first = items[0].clone(); let owned = items; println!("{} {}", first.qty, owned.len()); }
