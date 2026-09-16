struct D { v: i64 }
fn inner(a: &&D) -> *const D {
    let x: &D = *a;
    return x as *const D;
}
fn main() {
    let o: Option<D> = Option::Some(D { v: 6i64 });
    let mut bad: i32 = 0i32;
    match &o {
        Option::Some(rd) => {
            if inner(&rd) != rd as *const D { bad = 1i32; }
        }
        Option::None => { std::process::exit(90); }
    }
    if bad != 0i32 { std::process::exit(bad); }
    if let Option::Some(rd) = &o {
        if inner(&rd) != rd as *const D { std::process::exit(2); }
    }
    match o {
        Option::Some(ref rd) => {
            if inner(&rd) != rd as *const D { std::process::exit(3); }
        }
        Option::None => { std::process::exit(91); }
    }
    std::process::exit(0);
}
