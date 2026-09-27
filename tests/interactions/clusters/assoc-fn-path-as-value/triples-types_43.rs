#[derive(Clone, Copy, PartialEq, Debug)]
enum Light { Red, Yellow { secs: i64 }, Green(i64) }
impl Light {
    fn next(self) -> Self { match self { Self::Red => Self::Green(30), Self::Green(t) if t > 10 => Self::Yellow { secs: t / 10 }, Self::Green(_) => Self::Yellow { secs: 1 }, Self::Yellow { .. } => Self::Red } }
    fn default() -> Self { Light::Red }
    fn wait(&self) -> i64 { match *self { Light::Red => 60, Light::Yellow { secs } => secs, Light::Green(t) => t } }
    fn is_go(&self) -> bool { matches!(self, Light::Green(_)) }
    fn cycle(self, n: usize) -> Vec<Self> { let mut out = vec![self]; let mut cur = self; for _ in 0..n { cur = cur.next(); out.push(cur); } out }
}
fn main() {
    let c = Light::default().cycle(5);
    for l in &c { print!("{} {} | ", l.wait(), l.is_go()); }
    println!();
    println!("{}", Light::Green(5).next() == Light::Yellow { secs: 1 });
    let total: i64 = c.iter().map(Light::wait).sum();
    println!("{}", total);
}
