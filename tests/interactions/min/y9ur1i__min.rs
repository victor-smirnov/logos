struct T<K,V> { fk: K, fv: V, c1: Option<Box<T<K,V>>>, c2: Option<Box<T<K,V>>>, }
fn main() { let t: Option<Box<T<i32,i32>>> = Some(Box::new(T { fk: 1, fv: 1, c1: None, c2: None, })); println!("{}", t.is_some()); let _ = t.map(|b| (b.fk, b.fv, b.c1.is_none(), b.c2.is_none())); }
