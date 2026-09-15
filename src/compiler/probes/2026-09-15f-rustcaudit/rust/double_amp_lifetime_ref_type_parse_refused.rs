struct D { v: i64 }
fn read<'a>(src: &&'a D) -> i64 {
    return src.v;
}
fn logos_main() -> i32 {
    let d: D = D { v: 6i64 };
    let rd: &D = &d;
    return (read(&rd) - 6i64) as i32;
}

fn main() { std::process::exit(logos_main()); }
