#[derive(Debug)]
enum AppErr { Parse(i32), Neg(i64), Missing }
impl From<i32> for AppErr { fn from(c: i32) -> Self { return AppErr::Parse(c); } }
trait Stage { fn run(&self, x: i64) -> Result<i64, AppErr>; fn name(&self) -> String; }
struct Double;
struct CheckPos;
struct Fail { code: i32 }
impl Stage for Double { fn run(&self, x: i64) -> Result<i64, AppErr> { return Ok(x * 2); } fn name(&self) -> String { return String::from("dbl"); } }
impl Stage for CheckPos { fn run(&self, x: i64) -> Result<i64, AppErr> { if x < 0 { return Err(AppErr::Neg(x)); } return Ok(x); } fn name(&self) -> String { return String::from("pos"); } }
fn raw(code: i32) -> Result<i64, i32> { return Err(code); }
impl Stage for Fail { fn run(&self, _x: i64) -> Result<i64, AppErr> { let v = raw(self.code)?; return Ok(v); } fn name(&self) -> String { return String::from("fail"); } }
fn pipeline(stages: &Vec<Box<dyn Stage>>, x: i64) -> Result<i64, AppErr> {
    let mut v = x;
    for s in stages.iter() { v = s.run(v)?; }
    return Ok(v);
}
fn find<'a>(stages: &'a Vec<Box<dyn Stage>>, n: &str) -> Option<&'a dyn Stage> {
    for s in stages.iter() { if s.name().as_str() == n { return Some(s.as_ref()); } }
    return None;
}
fn run_named(stages: &Vec<Box<dyn Stage>>, n: &str, x: i64) -> Result<i64, AppErr> {
    let s = find(stages, n).ok_or(AppErr::Missing)?;
    return s.run(x);
}
fn main() {
    let mut st: Vec<Box<dyn Stage>> = Vec::new();
    st.push(Box::new(Double)); st.push(Box::new(CheckPos)); st.push(Box::new(Double));
    println!("{:?}", pipeline(&st, 3));
    println!("{:?}", pipeline(&st, -3));
    st.push(Box::new(Fail { code: 7 }));
    println!("{:?}", pipeline(&st, 1));
    println!("{:?}", run_named(&st, "pos", -9));
    println!("{:?}", run_named(&st, "zzz", 1));
    match run_named(&st, "dbl", 21) { Ok(v) => println!("ok {}", v), Err(e) => println!("err {:?}", e) }
    let o: Option<&dyn Stage> = find(&st, "fail");
    if let Some(s) = o { println!("found {}", s.name()); }
}
