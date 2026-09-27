


enum Cmd { Push(i64), Pop }
fn run(stack: &mut Vec<i64>, c: Cmd) { match c { Cmd::Push(v) => stack.push(v), Cmd::Pop => { stack.pop(); } } }
fn main() {
    let mut stack: Vec<i64> = Vec::new();
    run(&mut stack, Cmd::Push(1)); run(&mut stack, Cmd::Pop);
    let r = match Cmd::Pop { Cmd::Push(v) => stack.push(v), Cmd::Pop => { stack.pop(); } };
    println!("{}", stack.len());
}
