



trait Named {
    fn name(&self) -> &str;
    fn first_char(&self) -> u8 { let n = self.name(); return n.as_bytes()[0]; }
    fn longer<'a>(&'a self, other: &'a str) -> &'a str { if self.name().len() >= other.len() { return self.name(); } return other; }
}
trait Tagged: Named { fn tag(&self) -> i64; fn describe(&self) -> String { return format!("{}#{}", self.name(), self.tag()); } }
struct Person { nm: String, id: i64 }
impl Named for Person { fn name(&self) -> &str { return self.nm.as_str(); } }
impl Tagged for Person { fn tag(&self) -> i64 { return self.id; } }
trait Bump { fn bump(&mut self) -> &mut i64; }
struct Ctr { v: i64 }
impl Bump for Ctr { fn bump(&mut self) -> &mut i64 { self.v += 1; return &mut self.v; } }
fn best<'a, T: Named>(xs: &'a [T]) -> &'a T {
    let mut b = &xs[0];
    for x in xs.iter() { if x.name().len() > b.name().len() { b = x; } }
    return b;
}
fn main() {
    let p = Person { nm: String::from("alice"), id: 7 };
    println!("{} {} {}", p.name(), p.first_char(), p.longer("bo"));
    println!("{}", p.describe());
    let mut c = Ctr { v: 10 };
    {
        let r = c.bump();
        *r *= 2;
    }
    let r2: &mut i64 = c.bump();
    *r2 += 100;
    println!("{}", c.v);
    let ps: Vec<Person> = vec![Person { nm: String::from("x"), id: 1 }, Person { nm: String::from("yyyy"), id: 2 }, Person { nm: String::from("zz"), id: 3 }];
    let b = best(&ps);
    println!("{} {}", b.name(), b.tag());
    let rp: &Person = &p;
    let rrp: &&Person = &rp;
    println!("{} {}", rrp.name(), rrp.describe());
}
