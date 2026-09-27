use std::collections::{HashMap, BTreeMap, BTreeSet, HashSet, VecDeque};
use std::rc::Rc;
trait Animal { fn legs(&self) -> i64; }
struct Dog { id: i64 }
impl Animal for Dog { fn legs(&self) -> i64 { return 4; } }
impl Drop for Dog { fn drop(&mut self) { println!("drop dog{}", self.id); } }
fn main() {
    {
        let mut dq: VecDeque<Box<dyn Animal>> = VecDeque::new();


    dq.push_back(Box::new(Dog { id: 1 }));

    let x = dq.pop_front();
        if let Some(a) = x { println!("popped {}", a.legs()); }
    }
    println!("--");
    {
        let mut dq: VecDeque<Box<dyn Animal>> = VecDeque::new();


    dq.push_back(Box::new(Dog { id: 2 }));
    }
    println!("--");
    {
        let mut m: BTreeMap<i64, Rc<dyn Animal>> = BTreeMap::new();


    m.insert(1, Rc::new(Dog { id: 3 }));
    }
    println!("--");
    {
        let mut m: HashMap<i64, Box<dyn Animal>> = HashMap::new();



    m.insert(1, Box::new(Dog { id: 4 }));
    let old = m.insert(1, Box::new(Dog { id: 5 }));
        println!("old some {}", old.is_some());
    }
    println!("end");
}
