trait Scorer { fn score(&self, x: i64) -> i64; fn best<I: Iterator<Item = i64>>(&self, it: I) -> i64 { let mut m = i64::MIN; for x in it { let s = self.score(x); if s > m { m = s; } } m } }
struct Neg; struct Mod(i64);
impl Scorer for Neg { fn score(&self, x: i64) -> i64 { -x } }
impl Scorer for Mod { fn score(&self, x: i64) -> i64 { x % self.0 } }
struct Countdown(i64);
impl Iterator for Countdown { type Item = i64; fn next(&mut self) -> Option<i64> { if self.0 == 0 { None } else { self.0 -= 1; Some(self.0 + 1) } } }
fn total_score<S: Scorer>(s: &S, xs: &[i64]) -> i64 { xs.iter().map(|&x| s.score(x)).sum::<i64>() }
fn main() {
    let v = vec![3i64, 9, 4, 7];
    println!("{}", Neg.best(v.iter().cloned()));
    println!("{}", Mod(5).best(v.iter().copied()));
    println!("{}", Mod(4).best(Countdown(6)));
    println!("{}", total_score(&Mod(3), &v));
    let mut cd = Countdown(3);
    let first = cd.next();
    let rest: Vec<i64> = cd.collect();
    println!("{:?} {:?}", first, rest);
    let mut acc = Vec::new();
    for (a, b) in Countdown(4).zip(Countdown(2)) { acc.push(a * b); }
    println!("{:?}", acc);
    let s: i64 = Countdown(5).step_by(2).map(|x| x * x).sum();
    println!("{}", s);
}
