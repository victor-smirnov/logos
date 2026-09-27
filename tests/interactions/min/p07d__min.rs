fn main() { let v = vec![1]; let f = move || v; let _a = f(); let _b = f(); }
