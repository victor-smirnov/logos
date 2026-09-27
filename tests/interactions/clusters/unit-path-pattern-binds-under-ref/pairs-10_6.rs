struct Inner { v: i64, tag: Option<i64> }
fn d(o: &Inner) -> i64 { match o { Inner { tag: None, v } => *v, Inner { tag: Some(t), .. } => 1000 + *t } }
fn main() { let o = Inner { v: 5, tag: Some(7) }; println!("{}", d(&o)); }
