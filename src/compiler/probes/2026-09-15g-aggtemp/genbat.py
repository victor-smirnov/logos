#!/usr/bin/env python3
"""genbat.py OUTDIR — round 2026-09-15g-aggtemp hand battery: a droppable RVALUE temporary in a non-consuming position.
Every program: fn g(p) -> i64 { BODY }, main prints r and n; the oracle is stdout equality with the rustc twin."""
import sys, os
HEAD = """package {name};
extern fn printf(fmt: *const u8, ...) -> i32;
struct D {{ v: i64, c: *mut i64 }}
impl Drop for D {{ fn drop(self: &mut D) {{ unsafe {{ *self.c = *self.c + self.v; }} }} }}
struct W {{ x: D, y: D, z: i64 }}
impl D {{ fn get(self: &D) -> i64 {{ return self.v; }} }}
fn mk(v: i64, p: *mut i64) -> D {{ return D {{ v: v, c: p }}; }}
fn mkarr(p: *mut i64) -> [D; 2] {{ return [D {{ v: 1i64, c: p }}, D {{ v: 10i64, c: p }}]; }}
fn mktup(p: *mut i64) -> (D, D) {{ return (D {{ v: 1i64, c: p }}, D {{ v: 10i64, c: p }}); }}
fn sumref(a: &[D; 2]) -> i64 {{ return a[0].v + a[1].v; }}
fn rd(p: *mut i64) -> i64 {{ return unsafe {{ *p }}; }}
struct Q {{ f: fn(*mut i64) -> i64, d: D }}
fn seven(p: *mut i64) -> i64 {{ return 7i64; }}
fn g(p: *mut i64) -> i64 {{
{body}
}}
fn main() -> i32 {{
    let mut n: i64 = 0i64;
    let p: *mut i64 = &mut n;
    let r: i64 = g(p);
    let got: i64 = rd(p);
    unsafe {{ printf("r=%ld n=%ld\\n".as_ptr(), r, got); }}
    return 0i32;
}}
"""
D1 = "D { v: 1i64, c: p }"; D10 = "D { v: 10i64, c: p }"; D100 = "D { v: 100i64, c: p }"; D1000 = "D { v: 1000i64, c: p }"
A = f"[{D1}, {D10}]"; T = f"({D1}, {D10})"
P = {
 "a01_arrlit_index_field": f"    let k: i64 = {A}[1].v;\n    return k;",
 "a02_tuplit_index_field": f"    let k: i64 = {T}.1.v;\n    return k;",
 "a03_arrlit_len": f"    let k: i64 = {A}.len() as i64;\n    return k;",
 "a04_for_arrlit": f"    let mut s: i64 = 0i64;\n    for d in {A} {{\n        s = s + d.v;\n    }}\n    return s;",
 "a05_let_ref_arrlit": f"    let r: &[D; 2] = &{A};\n    return r[1].v;",
 "a06_block_tail_arrlit_index": f"    let k: i64 = {{ {A} }}[0].v;\n    return k;",
 "a07_arrlit_stmt_expr": f"    {A};\n    return 3i64;",
 "a08_match_arrlit_wild": f"    let k: i64 = match {A} {{\n        _ => 5i64,\n    }};\n    return k;",
 "a09_nested_arrlit_index": f"    let k: i64 = [[{D1}], [{D10}]][1][0].v;\n    return k;",
 "a10_call_arr_index_field": "    let k: i64 = mkarr(p)[1].v;\n    return k;",
 "a11_call_tup_index_field": "    let k: i64 = mktup(p).1.v;\n    return k;",
 "a12_two_arrlit_temps_one_stmt": f"    let k: i64 = {A}[1].v + [{D100}, {D1000}][0].v;\n    return k;",
 "a13_arrlit_temp_in_cond_return": f"    if {A}[1].v == 10i64 {{\n        return 7i64;\n    }}\n    return 8i64;",
 "a14_tuplit_copy_elem": f"    let k: i64 = ({D1}, 5i64).1;\n    return k;",
 "a15_filllit_rvalue_index": f"    let k: i64 = [{D1}; 1][0].v;\n    return k;",
 "a16_structlit_field_control": f"    let k: i64 = W {{ x: {D1}, y: {D10}, z: 3i64 }}.z;\n    return k;",
 "a17_arrlit_call_elems_index": "    let k: i64 = [mk(1i64, p), mk(10i64, p)][1].v;\n    return k;",
 "a18_let_ref_arrlit_index": f"    let k: &D = &{A}[1];\n    return k.v;",
 "a19_for_arrlit_iter": f"    let mut s: i64 = 0i64;\n    for d in {A}.iter() {{\n        s = s + d.v;\n    }}\n    return s;",
 "a20_loop_arrlit_index": f"    let mut s: i64 = 0i64;\n    let mut i: i64 = 0i64;\n    while i < 3i64 {{\n        s = s + {A}[1].v;\n        i = i + 1i64;\n    }}\n    return s;",
 "a21_let_ref_tuplit": f"    let t: &(D, D) = &{T};\n    return t.1.v;",
 "a22_call_arg_ref_arrlit": f"    let k: i64 = sumref(&{A});\n    return k;",
 "a23_let_slice_arrlit": f"    let s: &[D] = &{A};\n    return s.len() as i64;",
 "a24_arrlit_runtime_index": f"    let i: usize = 1usize;\n    let k: i64 = {A}[i].v;\n    return k;",
 "a25_tuplit_break_in_loop": f"    let mut s: i64 = 0i64;\n    loop {{\n        s = s + {T}.0.v;\n        if s > 0i64 {{\n            break;\n        }}\n    }}\n    return s;",
 "a26_arrlit_index_in_if_expr_arm": f"    let c: bool = true;\n    let k: i64 = if c {{ {A}[1].v }} else {{ 0i64 }};\n    return k;",
 "a27_call_arr_len": "    let k: i64 = mkarr(p).len() as i64;\n    return k;",
 "a28_call_for": "    let mut s: i64 = 0i64;\n    for d in mkarr(p) {\n        s = s + d.v;\n    }\n    return s;",
 "a29_arrlit_index_early_return_in_sibling": f"    let k: i64 = {A}[1].v + {{ if rd(p) == 0i64 {{ return 9i64; }} 0i64 }};\n    return k;",
 "a30_struct_field_arr_index": f"    let k: i64 = W {{ x: {D1}, y: {D10}, z: 3i64 }}.y.v;\n    return k;",

 # extension through a PROJECTION of a temporary (Rust extends the temporary: r[destructors.scope.lifetime-extension.exprs])
 "c01_let_ref_structlit_field": f"    let k: &D = &W {{ x: {D1}, y: {D10}, z: 3i64 }}.y;\n    return k.v + rd(p);",
 "c02_let_ref_arrlit_index_field": f"    let k: &i64 = &{A}[1].v;\n    return *k + rd(p);",
 "c03_let_ref_tuplit_index": f"    let k: &D = &{T}.1;\n    return k.v + rd(p);",
 "c04_match_scrut_arrlit_index_field": f"    let k: i64 = match {A}[1].v {{\n        10 => 1i64,\n        _ => 0i64,\n    }};\n    return k;",
 "c05_method_on_arrlit_elem": f"    let k: i64 = {A}[1].get();\n    return k;",
 "c06_store_into_arrlit_temp_field": f"    {A}[0].v = 5i64;\n    return 0i64;",
 "c07_nested_tuple_in_arrlit": f"    let k: i64 = [({D1}, {D10})][0].1.v;\n    return k;",
 "c08_arrlit_index_read_before_drop": f"    let k: i64 = {A}[1].v + rd(p);\n    return k;",
 "c09_filllit_len": f"    let k: i64 = [{D1}; 3].len() as i64;\n    return k;",
 "c10_call_arr_len_side_effect": "    let k: i64 = mkarr(p).len() as i64 + rd(p);\n    return k;",
 "c11_structlit_field_read_in_let_ref_then_call": f"    let k: &D = &W {{ x: {D1}, y: {D10}, z: 3i64 }}.y;\n    let q: i64 = mk(100i64, p).v;\n    return k.v + q + rd(p);",
 "c12_tuplit_len_like_method_eq": f"    let k: i64 = if {T}.0.get() == 1i64 {{ 1i64 }} else {{ 0i64 }};\n    return k;",

 # neighbours named by the dlog per-site read (projection sites with no owner fact in context)
 "d01_fru_base_temp": f"    let s: W = W {{ x: {D100}, ..W {{ x: {D1}, y: {D10}, z: 3i64 }} }};\n    return s.y.v + rd(p);",
 "d02_fnptr_field_call_on_temp": "    let k: i64 = Q { f: seven, d: mk(1i64, p) }.f(p);\n    return k;",
 "d03_let_ref_aggregate_extended_arrlit": f"    let t: (&[D; 2], i64) = (&{A}, 1i64);\n    return t.0[1].v + rd(p);",
 "d04_if_arm_let_ref_structlit_field": f"    let c: bool = true;\n    let k: &D = if c {{ &W {{ x: {D1}, y: {D10}, z: 3i64 }}.y }} else {{ &W {{ x: {D100}, y: {D1000}, z: 3i64 }}.x }};\n    return k.v + rd(p);",
 "d05_arrlit_index_in_while_cond": f"    let mut i: i64 = 0i64;\n    while {A}[1].v > i {{\n        i = i + 5i64;\n    }}\n    return i;",
 "d06_arrlit_index_mut_ctx_method": f"    let k: i64 = [{D1}, {D10}][1].get() + (W {{ x: {D100}, y: {D1000}, z: 3i64 }}).x.get();\n    return k;",
 # locals: right on base by cancellation (consumeland k08-t13); the union arm must keep them right
 "b01_locals_arrlit_index": f"    let a: D = {D1};\n    let b: D = {D10};\n    let k: i64 = [a, b][1].v;\n    return k;",
 "b02_locals_for": f"    let a: D = {D1};\n    let b: D = {D10};\n    let mut s: i64 = 0i64;\n    for d in [a, b] {{\n        s = s + d.v;\n    }}\n    return s;",
 "b03_locals_let_ref": f"    let a: D = {D1};\n    let b: D = {D10};\n    let r: &[D; 2] = &[a, b];\n    return r[0].v;",
 "b04_locals_return_arr": "    let a: D = mk(1i64, p);\n    let b: D = mk(10i64, p);\n    let arr: [D; 2] = [a, b];\n    return arr[1].v;",
 "b05_locals_tuplit_index": f"    let a: D = {D1};\n    let b: D = {D10};\n    let k: i64 = (a, b).1.v;\n    return k;",
}
out = sys.argv[1]
for name, body in P.items():
    open(os.path.join(out, name + ".logos"), "w").write(HEAD.format(name=name, body=body))
print(len(P))
