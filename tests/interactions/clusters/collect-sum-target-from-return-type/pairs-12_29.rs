trait Keyed { type K: Ord + Clone; fn key(&self) -> Self::K; }
struct Pt { x: i64 }
impl Keyed for Pt { type K = i64; fn key(&self) -> i64 { return self.x % 3; } }
fn ks1(v: &Vec<i64>) -> Vec<i64> { return v.iter().map(|x| x + 1).collect(); }
fn ks2<T: Keyed>(items: &Vec<T>) -> Vec<T::K> { return items.iter().map(|i| i.key()).collect(); }
fn ks3<T: Keyed>(items: &Vec<T>) -> Vec<T::K> { let r: Vec<T::K> = items.iter().map(|i| i.key()).collect(); return r; }
fn main() {
    let ps: Vec<Pt> = (0..4).map(|x| Pt { x }).collect();
    println!("{:?} {:?} {:?}", ks1(&vec![1, 2]), ks2(&ps), ks3(&ps));
}
