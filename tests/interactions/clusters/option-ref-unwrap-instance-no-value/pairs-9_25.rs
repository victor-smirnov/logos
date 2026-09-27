use std::cmp::Ordering;
#[derive(Clone, Copy, PartialEq, Eq, PartialOrd)]
struct Ver { major: i64 }
impl Ord for Ver { fn cmp(&self, o: &Ver) -> Ordering { self.major.cmp(&o.major) } }
fn main() { let vs = vec![Ver { major: 1 }, Ver { major: 5 }, Ver { major: 2 }]; let mx = vs.iter().max().unwrap(); println!("{}", mx.major); }
