fn main() {
    let bigu: u64 = 100; let smallu: u64 = 1;
    let ru1: &u64 = &bigu; let ru2: &u64 = &smallu; let lt_u = ru1 < ru2;
    let bigf: f64 = 100.0; let smallf: f64 = 1.0;
    let rf1: &f64 = &bigf; let rf2: &f64 = &smallf; let lt_f = rf1 < rf2;
    let bigi: i64 = 100; let smalli: i64 = 1;
    let ri1: &i64 = &bigi; let ri2: &i64 = &smalli; let lt_i = ri1 < ri2;
    let tb = true; let fb = false;
    let rb1: &bool = &tb; let rb2: &bool = &fb; let lt_b = rb1 < rb2;
    println!("lt_u={} lt_f={} lt_i={} lt_b={}", lt_u as i32, lt_f as i32, lt_i as i32, lt_b as i32);
    if lt_u { std::process::exit(1); }
    if lt_f { std::process::exit(2); }
    if lt_i { std::process::exit(3); }
    if lt_b { std::process::exit(4); }
}
