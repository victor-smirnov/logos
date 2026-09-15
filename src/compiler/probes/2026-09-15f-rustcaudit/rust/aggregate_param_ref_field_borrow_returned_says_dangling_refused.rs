struct L { value: i64 }
fn refs<'a>(p: (&'a L, &'a L)) -> Vec<&'a i64> {
    let mut r: Vec<&i64> = Vec::<&i64>::new();
    r.push(&p.0.value);
    r.push(&p.1.value);
    return r;
}
fn logos_main() -> i32 {
    let a: L = L { value: 3i64 };
    let b: L = L { value: 4i64 };
    let v: Vec<&i64> = refs((&a, &b));
    return (*v[0usize] + *v[1usize]) as i32 - 7i32;
}

fn main() { std::process::exit(logos_main()); }
