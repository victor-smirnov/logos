# Перепин ADR 0028: фикстуры, у которых СМЕНИЛСЯ КЛАСС ошибки

Корпус перепиннен под новый чекер: его формулировки — rustc'шные
(E0499/E0502/E0503/E0505/E0506/E0515/E0521/E0596/E0597), каждая
сверена с оракулом дословно. Программа во всех этих фикстурах
по-прежнему ОТВЕРГАЕТСЯ — иначе она попала бы в список настоящих
промахов и не перепинивалась бы.

Здесь собраны те, где новый чекер отвергает её ПО ДРУГОЙ ПРИЧИНЕ,
чем пинил старый. Часть — соседние коды rustc (E0597 против E0515,
E0521 против E0515), часть требует разбора.

⚠ Сравнение ведётся с пином, лежавшим в git до этого круга, а не с
промежуточным состоянием.

Всего сменили класс: 204 из 808

## tests/imported/fail/borrowck/borrowck-partial-reinit-3.expected
- было (E0382 moved): `use of moved field 'x.a' (moved on line 11)`
- стало (other): `assign to part of moved value 'x.a.f' at line 12`

## tests/imported/fail/borrowck/bindings-after-at-or-patterns-slice-patterns-box-patterns.expected
- было (E0499/E0502 borrow): `cannot borrow moved value 'x'`
- стало (E0382 moved): `use of moved or uninitialised 'x' at line 18`

## tests/imported/fail/moves/moves-based-on-type-distribute-copy-over-paren--move-into-struct-lit-then-borrow.expected
- было (E0499/E0502 borrow): `cannot borrow moved value 'x'`
- стало (E0382 moved): `use of moved or uninitialised 'x' at line 12`

## tests/imported/fail/moves/moves-based-on-type-distribute-copy-over-paren--p-move-into-struct-lit-paren.expected
- было (E0499/E0502 borrow): `cannot borrow moved value 'x'`
- стало (E0382 moved): `use of moved or uninitialised 'x' at line 12`

## tests/imported/fail/moves/moves-based-on-type-exprs.expected
- было (E0499/E0502 borrow): `cannot borrow moved value 'x'`
- стало (E0382 moved): `use of moved or uninitialised 'x' at line 18`

## tests/logos/fail/bc_movedvalue_borrow_carries_move_line_fail.expected
- было (E0499/E0502 borrow): `cannot borrow moved value 'a' (moved on line 15)`
- стало (E0382 moved): `use of moved or uninitialised 'a' at line 16`

## tests/spec/fail/borrow_diag_1__take-moved-cannot-borrow.expected
- было (E0499/E0502 borrow): `cannot borrow moved value 'x'`
- стало (E0382 moved): `use of moved or uninitialised 'x' at line 10`

## tests/imported/fail/nll/match-cfg-fake-edges2.expected
- было (E0499/E0502 borrow): `cannot borrow 'y' as shared: field of 'y' is mutably borrowed`
- стало (E0503 use): `cannot use 'y.b' because it was mutably borrowed`

## tests/logos/fail/bc_d1r11_x1_result_subplace.expected
- было (E0499/E0502 borrow): `cannot borrow 'c' as mutable: 'c' has shared borrows`
- стало (E0505 move): `cannot move out of 'h', which is behind a reference (line 18)`

## tests/imported/fail/borrowck/borrowck-issue-14498--box-mut-ref.expected
- было (E0499/E0502 borrow): `cannot borrow '*y' as mutable: 'y' has shared borrows`
- стало (E0506 assign): `cannot assign to 'y' because it is borrowed`

## tests/imported/fail/borrowck/borrowck-vec-pattern-move-tail.expected
- было (E0499/E0502 borrow): `cannot borrow 'a' as mutable: 'a.2' is already borrowed`
- стало (E0506 assign): `cannot assign to 'a[_]' because it is borrowed`

## tests/imported/fail/borrowck/borrowck-vec-pattern-nesting.expected
- было (E0499/E0502 borrow): `cannot borrow 'v' as mutable: 'v.0' is already borrowed`
- стало (E0506 assign): `cannot assign to 'v[_]' because it is borrowed`

## tests/imported/fail/borrowck/issue-81365-10.expected
- было (E0499/E0502 borrow): `cannot borrow 'self.container_field' as mutable: 'self' has shared borrows`
- стало (E0506 assign): `cannot assign to '*self' because it is borrowed`

## tests/imported/fail/borrowck/issue-81365-11.expected
- было (E0499/E0502 borrow): `cannot borrow 's.container_field': 's' is already mutably borrowed`
- стало (E0506 assign): `cannot assign to '*s' because it is borrowed`

## tests/imported/fail/borrowck/issue-81365-2.expected
- было (E0499/E0502 borrow): `cannot borrow 'self.container.container_field' as mutable: 'self.container' is already borrowed`
- стало (E0506 assign): `cannot assign to 'self.container' because it is borrowed`

## tests/imported/fail/borrowck/issue-81365-3.expected
- было (E0499/E0502 borrow): `cannot borrow 'self.container.container_field' as mutable: 'self.container' is already borrowed`
- стало (E0506 assign): `cannot assign to 'self.container' because it is borrowed`

## tests/imported/fail/borrowck/issue-81365-4--d2.expected
- было (E0499/E0502 borrow): `cannot borrow 'c.cf' as mutable: 'c' has shared borrows`
- стало (E0506 assign): `cannot assign to 'c' because it is borrowed`

## tests/imported/fail/borrowck/issue-81365-4--rd2.expected
- было (E0499/E0502 borrow): `cannot borrow 'c.cf' as mutable: 'c' has shared borrows`
- стало (E0506 assign): `cannot assign to 'c' because it is borrowed`

## tests/imported/fail/borrowck/issue-81365-5.expected
- было (E0499/E0502 borrow): `cannot borrow 'self.container_field' as mutable: 'self' has shared borrows`
- стало (E0506 assign): `cannot assign to '*self' because it is borrowed`

## tests/imported/fail/borrowck/issue-81365-6.expected
- было (E0499/E0502 borrow): `cannot borrow 'c.flag' as mutable: 'c' has shared borrows`
- стало (E0506 assign): `cannot assign to 'c' because it is borrowed`

## tests/imported/fail/borrowck/issue-81365-8.expected
- было (E0499/E0502 borrow): `cannot borrow 'self.container_field' as mutable: 'self' has shared borrows`
- стало (E0506 assign): `cannot assign to '*self' because it is borrowed`

## tests/imported/fail/borrowck/issue-81365-9--e-finding-call-returns-borrow-of-self.expected
- было (E0499/E0502 borrow): `cannot borrow 'self.container_field' as mutable: 'self' has shared borrows`
- стало (E0506 assign): `cannot assign to '*self' because it is borrowed`

## tests/imported/fail/borrowck/issue-81365-9--explicit-deref-call-borrow-then-write.expected
- было (E0499/E0502 borrow): `cannot borrow 'self.container_field' as mutable: 'self' has shared borrows`
- стало (E0506 assign): `cannot assign to '*self' because it is borrowed`

## tests/imported/fail/borrowck/issue-81365-9--g-method-call-deref.expected
- было (E0499/E0502 borrow): `cannot borrow 'self.container_field' as mutable: 'self' has shared borrows`
- стало (E0506 assign): `cannot assign to '*self' because it is borrowed`

## tests/imported/fail/nll/loan-ends-mid-block-pair.expected
- было (E0499/E0502 borrow): `cannot borrow 'data.0': 'data.0' is already mutably borrowed`
- стало (E0506 assign): `cannot assign to 'data.0' because it is borrowed`

## tests/imported/fail/nll/region-ends-after-if-condition.expected
- было (E0499/E0502 borrow): `cannot borrow 's.field' as mutable: 's.field' is already borrowed`
- стало (E0506 assign): `cannot assign to 's.field' because it is borrowed`

## tests/imported/fail/nll/return-from-loop.expected
- было (E0499/E0502 borrow): `cannot borrow 'my_struct.field': 'my_struct.field' is already mutably borrowed`
- стало (E0506 assign): `cannot assign to 'my_struct.field' because it is borrowed`

## tests/logos/fail/alias_mut_through_ref.expected
- было (E0499/E0502 borrow): `cannot borrow 'a.v' as mutable: 'a' has shared borrows`
- стало (E0506 assign): `cannot assign to '*a' because it is borrowed`

## tests/logos/fail/bc_0914e_thruref_hb_m17_refuse.expected
- было (E0499/E0502 borrow): `[fn main]: cannot borrow 's1.a' as mutable: 's1' has shared borrows`
- стало (E0506 assign): `cannot assign to 's1' because it is borrowed`

## tests/logos/fail/bc_0914f_thrurefland_hb_d14_refuse.expected
- было (E0499/E0502 borrow): `[fn main]: cannot borrow 's1.a': 's1' is already mutably borrowed`
- стало (E0506 assign): `cannot assign to 's1' because it is borrowed`

## tests/logos/fail/bc_0914f_thrurefland_hb_o13_refuse.expected
- было (E0499/E0502 borrow): `[fn main]: cannot borrow 's1.a': 's1' is already mutably borrowed`
- стало (E0506 assign): `cannot assign to 's1' because it is borrowed`

## tests/logos/fail/bc_0914f_thrurefland_hb_o14_refuse.expected
- было (E0499/E0502 borrow): `[fn main]: cannot borrow 's1.0': 's1' is already mutably borrowed`
- стало (E0506 assign): `cannot assign to 's1' because it is borrowed`

## tests/logos/fail/bc_0914f_thrurefland_hb_x11w_refuse.expected
- было (E0499/E0502 borrow): `[fn main]: cannot borrow 's1.a' as mutable: 's1' has shared borrows`
- стало (E0506 assign): `cannot assign to 's1' because it is borrowed`

## tests/logos/fail/bc_0914f_thrurefland_hb_x12w_refuse.expected
- было (E0499/E0502 borrow): `[fn main]: cannot borrow 's1.0' as mutable: 's1' has shared borrows`
- стало (E0506 assign): `cannot assign to 's1' because it is borrowed`

## tests/logos/fail/bc_0914f_thrurefland_hb_x18w_refuse.expected
- было (E0499/E0502 borrow): `[fn main]: cannot borrow 's1.b' as mutable: 's1' has shared borrows`
- стало (E0506 assign): `cannot assign to 's1' because it is borrowed`

## tests/logos/fail/bc_derefwrite_shared_borrow_fail.expected
- было (E0499/E0502 borrow): `cannot borrow 'r' as mutable: 'r' has shared borrows`
- стало (E0506 assign): `cannot assign to '*r' because it is borrowed`

## tests/logos/fail/bc_genrecv_field_write_while_held_fail.expected
- было (E0499/E0502 borrow): `cannot borrow 'c.v': 'c' is already mutably borrowed`
- стало (E0506 assign): `cannot assign to 'c' because it is borrowed`

## tests/logos/fail/bc_guard_tested_field_write_fail.expected
- было (E0499/E0502 borrow): `cannot borrow 'w.e' as mutable: 'w.e' is already borrowed`
- стало (E0506 assign): `cannot assign to 'w.e' because it is borrowed`

## tests/logos/fail/bc_mexprpat_binding_carried_fail.expected
- было (E0499/E0502 borrow): `cannot borrow 'y.f0': 'y.f0' is already mutably borrowed`
- стало (E0506 assign): `cannot assign to 'y.f0' because it is borrowed`

## tests/logos/fail/bc_mexprpat_refmut_loan_fail.expected
- было (E0499/E0502 borrow): `cannot borrow 'y.f0': 'y.f0' is already mutably borrowed`
- стало (E0506 assign): `cannot assign to 'y.f0' because it is borrowed`

## tests/logos/fail/bc_nll_d2_field_live_mut_refuse.expected
- было (E0499/E0502 borrow): `cannot borrow 's.a': 's.a' is already mutably borrowed`
- стало (E0506 assign): `cannot assign to 's.a' because it is borrowed`

## tests/logos/fail/bc_nll_d2_field_live_refuse.expected
- было (E0499/E0502 borrow): `cannot borrow 's.a' as mutable: 's.a' is already borrowed`
- стало (E0506 assign): `cannot assign to 's.a' because it is borrowed`

## tests/logos/fail/bc_thruref_place_retarget_refuse.expected
- было (E0499/E0502 borrow): `[fn main]: cannot borrow 'h.o' as mutable: 'h.o.0' is already borrowed`
- стало (E0506 assign): `cannot assign to 'h.o.0' because it is borrowed`

## tests/logos/fail/rfc2229_field_path_exclusivity.expected
- было (E0499/E0502 borrow): `cannot borrow 'p.x': 'p.x' is already mutably borrowed`
- стало (E0506 assign): `cannot assign to 'p.x' because it is borrowed`

## tests/logos/fail/bc_d1r5_h3_stmt_order_chain.expected
- было (E0499/E0502 borrow): `cannot borrow 'c' as mutable: 'c' has shared borrows`
- стало (E0597 outlives): `'z' does not live long enough: it is borrowed here and used after 'z' goes out of scope`

## tests/imported/fail/borrowck/accidentally-cloning-ref-borrow-error.expected
- было (E0499/E0502 borrow): `cannot borrow 'sr.x' as mutable: 'sr' is behind a `&` reference`
- стало (other): `'sr' is written or mutably borrowed behind a `&` reference (line 10)`

## tests/imported/fail/borrowck/borrow-raw-address-of-deref-mutability.expected
- было (E0499/E0502 borrow): `cannot borrow 'x' as mutable: 'x' is behind a `&` reference`
- стало (other): `'x' is written or mutably borrowed behind a `&` reference (line 8)`

## tests/imported/fail/borrowck/borrowck-access-permissions--d-mut-borrow-deref-shared-ref.expected
- было (E0499/E0502 borrow): `cannot borrow 'rx' as mutable: 'rx' is behind a `&` reference`
- стало (other): `'rx' is written or mutably borrowed behind a `&` reference (line 9)`

## tests/imported/fail/borrowck/borrowck-borrow-mut-base-ptr-in-aliasable-loc--c-mut-reborrow-from-shared.expected
- было (E0499/E0502 borrow): `cannot borrow 't0' as mutable: 't0' is behind a `&` reference`
- стало (other): `'t0' is written or mutably borrowed behind a `&` reference (line 7)`

## tests/imported/fail/borrowck/borrowck-closures-mut-of-imm--ctl.expected
- было (E0499/E0502 borrow): `cannot borrow 'x' as mutable: 'x' is behind a `&` reference`
- стало (other): `'x' is written or mutably borrowed behind a `&` reference (line 9)`

## tests/imported/fail/borrowck/cannot-borrow-index-of-hashmap-in-for.expected
- было (E0499/E0502 borrow): `cannot borrow 't' as mutable: not declared as mut`
- стало (other): `'_t4' is written or mutably borrowed behind a `&` reference (line 19)`

## tests/imported/fail/borrowck/mutable-borrow-behind-reference-61623.expected
- было (E0499/E0502 borrow): `cannot borrow 'x.b' as mutable: 'x' is behind a `&` reference`
- стало (other): `'x' is written or mutably borrowed behind a `&` reference (line 9)`

## tests/imported/fail/nll/dont-print-desugared.expected
- было (E0499/E0502 borrow): `cannot borrow 's.n' as mutable: 's' is behind a `&` reference`
- стало (other): `'s' is written or mutably borrowed behind a `&` reference (line 7)`

## tests/logos/fail/bc_recvaot_ref_to_mutref_local_fail.expected
- было (E0499/E0502 borrow): `cannot borrow 'rr' as mutable: 'rr' is behind a `&` reference`
- стало (other): `'rr' is written or mutably borrowed behind a `&` reference (line 11)`

## tests/logos/fail/bc_thru_ref_field_mut_reborrow_fail.expected
- было (E0499/E0502 borrow): `cannot borrow 'h.r' as mutable: 'h' is behind a `&` reference`
- стало (other): `'h' is written or mutably borrowed behind a `&` reference (line 12)`

## tests/logos/fail/bc_thru_ref_param_double_deref_fail.expected
- было (E0499/E0502 borrow): `cannot borrow 't0' as mutable: 't0' is behind a `&` reference`
- стало (other): `'t0' is written or mutably borrowed behind a `&` reference (line 8)`

## tests/logos/fail/bc_thru_ref_var_mut_reborrow_fail.expected
- было (E0499/E0502 borrow): `cannot borrow 'rx' as mutable: 'rx' is behind a `&` reference`
- стало (other): `'rx' is written or mutably borrowed behind a `&` reference (line 16)`

## tests/imported/fail/borrowck/borrowck-anon-fields-struct.expected
- было (E0503 use): `cannot use 'y' while 'y.f0' is mutably borrowed`
- стало (E0499/E0502 borrow): `cannot borrow 'y.f0' as mutable more than once at a time`

## tests/imported/fail/borrowck/borrowck-borrow-from-owned-ptr--a-field-mut-mut.expected
- было (E0503 use): `cannot use 'foo.bar1' while 'foo.bar1' is mutably borrowed`
- стало (E0499/E0502 borrow): `cannot borrow 'foo.bar1' as mutable more than once at a time`

## tests/imported/fail/borrowck/borrowck-borrow-from-owned-ptr--b-field-mut-imm.expected
- было (E0503 use): `cannot use 'foo.bar1' while 'foo.bar1' is mutably borrowed`
- стало (E0499/E0502 borrow): `cannot borrow 'foo.bar1' as immutable because it is also borrowed as mutable`

## tests/imported/fail/borrowck/borrowck-borrow-from-owned-ptr--d-subfield-mut-then-base-imm.expected
- было (E0503 use): `cannot use 'foo.bar1' while 'foo.bar1.int1' is mutably borrowed`
- стало (E0499/E0502 borrow): `cannot borrow 'foo.bar1.int1' as immutable because it is also borrowed as mutable`

## tests/imported/fail/borrowck/borrowck-borrow-from-stack-variable--base-imm.expected
- было (E0503 use): `cannot use 'foo.bar1' while 'foo.bar1.int1' is mutably borrowed`
- стало (E0499/E0502 borrow): `cannot borrow 'foo.bar1.int1' as immutable because it is also borrowed as mutable`

## tests/imported/fail/borrowck/borrowck-borrow-from-stack-variable--mi.expected
- было (E0503 use): `cannot use 'foo.bar1' while 'foo.bar1' is mutably borrowed`
- стало (E0499/E0502 borrow): `cannot borrow 'foo.bar1' as immutable because it is also borrowed as mutable`

## tests/imported/fail/borrowck/borrowck-borrow-from-stack-variable--mm.expected
- было (E0503 use): `cannot use 'foo.bar1' while 'foo.bar1' is mutably borrowed`
- стало (E0499/E0502 borrow): `cannot borrow 'foo.bar1' as mutable more than once at a time`

## tests/imported/fail/borrowck/borrowck-borrow-mut-object-twice.expected
- было (E0503 use): `cannot use 'x' while it is mutably borrowed`
- стало (E0499/E0502 borrow): `cannot borrow '*x' as mutable more than once at a time`

## tests/imported/fail/borrowck/borrowck-field-sensitivity--c-two-mut-borrows-same-field.expected
- было (E0503 use): `cannot use 'x.a' while 'x.a' is mutably borrowed`
- стало (E0499/E0502 borrow): `cannot borrow 'x.a' as mutable more than once at a time`

## tests/imported/fail/borrowck/borrowck-reborrow-from-mut.expected
- было (E0503 use): `cannot use 'foo.bar1' while 'foo.bar1' is mutably borrowed`
- стало (E0499/E0502 borrow): `cannot borrow 'foo.bar1' as mutable more than once at a time`

## tests/imported/fail/borrowck/borrowck-unboxed-closures-a.expected
- было (E0503 use): `cannot use 'f' while it is mutably borrowed`
- стало (E0499/E0502 borrow): `cannot borrow 'f' as immutable because it is also borrowed as mutable`

## tests/imported/fail/borrowck/borrowck-uniq-via-lend--b.expected
- было (E0503 use): `cannot use 'v' while it is mutably borrowed`
- стало (E0499/E0502 borrow): `cannot borrow 'v' as immutable because it is also borrowed as mutable`

## tests/imported/fail/borrowck/borrowck-uniq-via-lend--t18.expected
- было (E0503 use): `cannot use 'v' while it is mutably borrowed`
- стало (E0499/E0502 borrow): `cannot borrow 'v' as immutable because it is also borrowed as mutable`

## tests/imported/fail/borrowck/two-phase-nonrecv-autoref--a-fnmut-twice.expected
- было (E0503 use): `cannot use 'f' while it is mutably borrowed`
- стало (E0499/E0502 borrow): `cannot borrow '*f' as mutable more than once at a time`

## tests/imported/fail/borrowck/two-phase-nonrecv-autoref--byvalue-fnmut-twice.expected
- было (E0503 use): `cannot use 'f' while it is mutably borrowed`
- стало (E0499/E0502 borrow): `cannot borrow 'f' as mutable more than once at a time`

## tests/imported/fail/borrowck/two-phase-nonrecv-autoref--where-fnmut-twice.expected
- было (E0503 use): `cannot use 'f' while it is mutably borrowed`
- стало (E0499/E0502 borrow): `cannot borrow '*f' as mutable more than once at a time`

## tests/imported/fail/nll/closure-access-spans--a-closure-imm-capture-conflict.expected
- было (E0503 use): `cannot use 'x' while it is mutably borrowed`
- стало (E0499/E0502 borrow): `cannot borrow 'x' as immutable because it is also borrowed as mutable`

## tests/imported/fail/nll/issue-45157.expected
- было (E0503 use): `cannot use 'u' while it is mutably borrowed`
- стало (E0499/E0502 borrow): `cannot borrow 'u.s.a' as immutable because it is also borrowed as mutable`

## tests/imported/fail/nll/issue-57100.expected
- было (E0503 use): `cannot use 's' while it is mutably borrowed`
- стало (E0499/E0502 borrow): `cannot borrow 's.s1' as immutable because it is also borrowed as mutable`

## tests/imported/fail/borrowck/borrowck-assign-to-andmut-in-borrowed-loc.expected
- было (E0503 use): `cannot use 'y' while it is mutably borrowed`
- стало (E0506 assign): `cannot assign to 'y' because it is borrowed`

## tests/imported/fail/borrowck/borrowck-loan-of-static-data-issue-27616.expected
- было (E0503 use): `cannot use 's' while it is mutably borrowed`
- стало (E0506 assign): `cannot assign to '*s' because it is borrowed`

## tests/imported/fail/borrowck/borrowed-mut-pointer-assign-overflow-off.expected
- было (E0503 use): `cannot use 'y' while it is mutably borrowed`
- стало (E0506 assign): `cannot assign to 'y' because it is borrowed`

## tests/imported/fail/nll/guarantor-issue-46974.expected
- было (E0503 use): `cannot use 's' while it is mutably borrowed`
- стало (E0506 assign): `cannot assign to '*s' because it is borrowed`

## tests/imported/fail/borrowck/borrowck-autoref-3261.expected
- было (E0506 assign): `cannot assign to 'x' while it is mutably borrowed`
- стало (E0499/E0502 borrow): `cannot borrow 'x' as mutable more than once at a time`

## tests/imported/fail/borrowck/borrowck-closures-two-mut-fail.expected
- было (E0506 assign): `cannot assign to 'x' while it is mutably borrowed`
- стало (E0499/E0502 borrow): `cannot borrow 'x' as mutable more than once at a time`

## tests/imported/fail/borrowck/borrowck-closures-two-mut.expected
- было (E0506 assign): `cannot assign to 'x' while it is mutably borrowed`
- стало (E0499/E0502 borrow): `cannot borrow 'c1' as mutable, as it is not declared as mutable`

## tests/imported/fail/borrowck/borrowck-loan-blocks-mut-uniq.expected
- было (E0506 assign): `cannot assign to 'v' because it is borrowed`
- стало (E0499/E0502 borrow): `cannot borrow 'v' as mutable because it is also borrowed as immutable`

## tests/imported/fail/nll/closure-access-spans--b-closure-mut-capture-conflict.expected
- было (E0506 assign): `cannot assign to 'x' while it is mutably borrowed`
- стало (E0499/E0502 borrow): `cannot borrow 'x' as mutable more than once at a time`

## tests/imported/fail/nll/issue-27282-mutate-before-diverging-arm-2--guard-mutate.expected
- было (E0506 assign): `cannot assign to 'x' because it is borrowed`
- стало (E0499/E0502 borrow): `cannot borrow 'c' as mutable, as it is not declared as mutable`

## tests/imported/fail/nll/issue-27282-mutate-before-diverging-arm-3.expected
- было (E0506 assign): `cannot assign to 'x' because it is borrowed`
- стало (E0499/E0502 borrow): `cannot borrow 'g' as mutable, as it is not declared as mutable`

## tests/imported/fail/nll/issue-62007-assign-const-index.expected
- было (E0506 assign): `cannot assign through 'list[..]' while 'list' is mutably borrowed`
- стало (E0499/E0502 borrow): `cannot borrow 'b' as mutable more than once at a time`

## tests/imported/fail/nll/reference-carried-through-struct-field.expected
- было (E0506 assign): `cannot assign to 'x' while it is mutably borrowed`
- стало (E0503 use): `cannot use 'x' because it was mutably borrowed`

## tests/imported/fail/borrowck/borrowck-assign-to-andmut-in-aliasable-loc.expected
- было (E0506 assign): `cannot assign to 's.pointer': 's' is behind a `&` reference`
- стало (other): `'s' is written or mutably borrowed behind a `&` reference (line 6)`

## tests/imported/fail/borrowck/borrowck-borrow-mut-base-ptr-in-aliasable-loc--a-assign-through-shared-to-mut.expected
- было (E0506 assign): `cannot assign to 't1': 't1' is behind a `&` reference`
- стало (other): `'t1' is written or mutably borrowed behind a `&` reference (line 9)`

## tests/imported/fail/borrowck/borrowck-issue-14498--b-write-through-shared.expected
- было (E0506 assign): `cannot assign to 'p': 'p' is behind a `&` reference`
- стало (other): `'p' is written or mutably borrowed behind a `&` reference (line 10)`

## tests/logos/fail/bc_mutplace_shared_ref_box_param_write_fail.expected
- было (E0506 assign): `cannot assign to 'b': 'b' is behind a `&` reference`
- стало (other): `'b' is written or mutably borrowed behind a `&` reference (line 4)`

## tests/logos/fail/bc_write_thru_shared_ref_fail.expected
- было (E0506 assign): `cannot assign to 's.p': 's' is behind a `&` reference`
- стало (other): `'s' is written or mutably borrowed behind a `&` reference (line 11)`

## tests/imported/fail/borrowck/borrowck-fn-in-const-c.expected
- было (E0515 return-ref): `cannot return reference to local variable 'local': dangling reference`
- стало (E0505 move): `cannot move out of 'local.inner' because it is borrowed`

## tests/imported/fail/dropck/drop-with-active-borrows-2.expected
- было (E0515 return-ref): `cannot return reference to local variable 'out': dangling reference`
- стало (E0505 move): `cannot move out of 'raw' because it is borrowed`

## tests/imported/fail/lifetimes/return-reference-local-variable-13497.expected
- было (E0515 return-ref): `cannot return reference to local variable '__ret_tmp_0': dangling reference`
- стало (E0505 move): `cannot move out of 'raw' because it is borrowed`

## tests/logos/fail/bc_esc_holder_return_method_dangle.expected
- было (E0515 return-ref): `cannot return reference to local variable 'w': dangling reference`
- стало (E0505 move): `cannot move out of 'o' because it is borrowed`

## tests/logos/fail/wany_escapes_rc_container.expected
- было (E0515 return-ref): `cannot return reference to local variable 'e': dangling reference`
- стало (E0505 move): `cannot move out of 'h' because it is borrowed`

## tests/imported/fail/borrowck/borrowck-local-borrow-outlives-fn.expected
- было (E0515 return-ref): `cannot return reference to local variable 'x': dangling reference`
- стало (E0521 escapes): `borrowed data escapes outside of function: 'x'`

## tests/imported/fail/borrowck/borrowck-return-variable-on-stack-via-clone.expected
- было (E0515 return-ref): `cannot return reference to local variable '?': dangling reference`
- стало (E0521 escapes): `borrowed data escapes outside of function: 'x'`

## tests/imported/fail/nll/issue-68550.expected
- было (E0515 return-ref): `cannot return reference to local variable 'x': dangling reference`
- стало (E0521 escapes): `borrowed data escapes outside of function: 'x'`

## tests/logos/fail/bc_param_byvalue_escape_fail.expected
- было (E0515 return-ref): `cannot return reference to local variable 'x': dangling reference`
- стало (E0521 escapes): `borrowed data escapes outside of function: 'x'`

## tests/logos/fail/elision_wrong_param.expected
- было (E0515 return-ref): `cannot return reference to local variable 'b': dangling reference`
- стало (E0521 escapes): `borrowed data escapes outside of function: 'b'`

## tests/imported/fail/borrowck/issue-7573.expected
- было (E0597 outlives): `'installed' does not live long enough: it is borrowed by 'lines', which is used here after 'installed' goes out of scope (E0597)`
- стало (E0499/E0502 borrow): `cannot borrow 'f' as mutable, as it is not declared as mutable`

## tests/imported/fail/nll/escape-upvar-ref.expected
- было (E0597 outlives): `'y' does not live long enough: it is borrowed by 'p', which is used here after 'y' goes out of scope (E0597)`
- стало (E0499/E0502 borrow): `cannot borrow 'c' as mutable, as it is not declared as mutable`

## tests/imported/fail/nll/issue-54556-used-vs-unused-tails.expected
- было (E0597 outlives): `'t1' does not live long enough: it is borrowed by 'r', which is used here after 't1' goes out of scope (E0597)`
- стало (E0505 move): `cannot move out of 't1' because it is borrowed`

## tests/logos/fail/bc_esc_fnptr_param_dangle.expected
- было (E0597 outlives): `'owner' does not live long enough`
- стало (E0505 move): `cannot move out of 'owner' because it is borrowed`

## tests/logos/fail/bc_esc_generic_outparam_dangle.expected
- было (E0597 outlives): `'o' does not live long enough: it is borrowed by 'v', which is used here after 'o' goes out of scope (E0597)`
- стало (E0505 move): `cannot move out of 'o' because it is borrowed`

## tests/logos/fail/bc_0914m_storeedgeland_hb_c06_refuse.expected
- было (E0597 outlives): `[fn main]: 'd' does not live long enough: it is borrowed by 'buffer', which is used here after 'd' goes out of scope (E0597)`
- стало (E0506 assign): `cannot assign to 'd' because it is borrowed`

## tests/logos/fail/bc_0914m_storeedgeland_hb_c19_refuse.expected
- было (E0597 outlives): `[fn main]: 'data' does not live long enough: it is borrowed by 'buffer', which is used here after 'data' goes out of scope (E0597)`
- стало (E0506 assign): `cannot assign to 'data' because it is borrowed`

## tests/logos/fail/bc_0914m_storeedgeland_hb_l05_refuse.expected
- было (E0597 outlives): `[fn main]: 'data' does not live long enough: it is borrowed by 'buffer', which is used here after 'data' goes out of scope (E0597)`
- стало (E0506 assign): `cannot assign to 'data' because it is borrowed`

## tests/imported/fail/nll/issue-52534-2.expected
- было (E0597 outlives): `'x2' does not live long enough: it is borrowed by 'y', which is used here after 'x2' goes out of scope (E0597)`
- стало (E0515 return-ref): `cannot return reference to local variable 'x2': dangling reference`

## tests/imported/fail/regions/regions-free-region-ordering-caller1.expected
- было (lifetime mismatch): `temporary value dropped while borrowed: 'z' borrows a temporary, but its declared type requires lifetime 'a, which outlives this function (E0716)`
- стало (E0597 outlives): `'_t4' does not live long enough: it is borrowed here and used after '_t4' goes out of scope`

## tests/logos/fail/bc_letbind_temp_named.expected
- было (lifetime mismatch): `temporary value dropped while borrowed: 'z' borrows a temporary, but its declared type requires lifetime 'a, which outlives this function (E0716)`
- стало (E0597 outlives): `'_t4' does not live long enough: it is borrowed here and used after '_t4' goes out of scope`

## tests/imported/fail/borrowck/borrowck-uninit-field-access.expected
- было (other): `partially moved`
- стало (E0382 moved): `use of moved or uninitialised 'line2.middle' at line 15`

## tests/imported/fail/moves/move-deref-coercion.expected
- было (other): `partially moved`
- стало (E0382 moved): `use of moved or uninitialised 'val.first' at line 17`

## tests/imported/fail/moves/moves-based-on-type-match-bindings.expected
- было (other): `use of partially moved value 'x' (field 'f' moved on line 12)`
- стало (E0382 moved): `use of moved or uninitialised 'x.f' at line 13`

## tests/imported/fail/nll/borrowck-partial-field-move.expected
- было (other): `partially moved`
- стало (E0382 moved): `use of moved or uninitialised 'o.i' at line 14`

## tests/imported/fail/nll/partial-move-borrow-after-move.expected
- было (other): `moved field`
- стало (E0382 moved): `use of moved or uninitialised 'o.i' at line 14`

## tests/imported/fail/nll/partial-move-field-then-whole.expected
- было (other): `partially moved`
- стало (E0382 moved): `use of moved or uninitialised 'o.i' at line 15`

## tests/imported/fail/nll/partial-move-return-whole.expected
- было (other): `partially moved`
- стало (E0382 moved): `use of moved or uninitialised 'o.i' at line 14`

## tests/imported/fail/nll/partial-move-same-field-twice.expected
- было (other): `moved field`
- стало (E0382 moved): `use of moved or uninitialised 'o.i' at line 14`

## tests/logos/fail/bc_0908_subslice_hb_hp_full_rest_refuse.expected
- было (other): `[fn main]: use of partially moved value 'a'`
- стало (E0382 moved): `use of moved or uninitialised 'a.[0]' at line 12`

## tests/logos/fail/bc_partpair_struct_shorthand_field_moved.expected
- было (other): `use of partially moved value 'x' (field 'f' moved on line 29)`
- стало (E0382 moved): `use of moved or uninitialised 'x.f' at line 34`

## tests/logos/fail/bc_recvpartial_byval_recv_fail.expected
- было (other): `use of partially moved value 'l'`
- стало (E0382 moved): `use of moved or uninitialised 'l.origin' at line 17`

## tests/logos/fail/bc_recvpartial_shared_recv_fail.expected
- было (other): `use of partially moved value 'l'`
- стало (E0382 moved): `use of moved or uninitialised 'l.origin' at line 23`

## tests/logos/fail/cond_move_glue_name_move_fail.expected
- было (other): `use of partially moved value 'h' (field 'p' moved on line 16)`
- стало (E0382 moved): `use of moved or uninitialised 'h.p' at line 17`

## tests/logos/fail/partial_move_deep_whole.expected
- было (other): `partially moved`
- стало (E0382 moved): `use of moved or uninitialised 'o.i.s' at line 10`

## tests/logos/fail/partial_move_root_use_ctl.expected
- было (other): `use of partially moved value 'x' (field 'a' moved on line`
- стало (E0382 moved): `use of moved or uninitialised 'x.a' at line 24`

## tests/imported/fail/nll/df-field-then-whole.expected
- было (other): `field of 'p' is already borrowed`
- стало (E0499/E0502 borrow): `cannot borrow 'p.a' as mutable more than once at a time`

## tests/imported/fail/nll/df-grandchild-prefix.expected
- было (other): `already mutably borrowed`
- стало (E0499/E0502 borrow): `cannot borrow 'o.a' as mutable more than once at a time`

## tests/imported/fail/nll/df-method-vs-field-borrow.expected
- было (other): `already mutably borrowed`
- стало (E0499/E0502 borrow): `cannot borrow 'p.a' as mutable more than once at a time`

## tests/imported/fail/nll/df-overlap-via-prefix.expected
- было (other): `already mutably borrowed`
- стало (E0499/E0502 borrow): `cannot borrow 'o.i' as mutable more than once at a time`

## tests/imported/fail/nll/df-prefix-conflict.expected
- было (other): `is already mutably borrowed`
- стало (E0499/E0502 borrow): `cannot borrow 'o.i' as mutable more than once at a time`

## tests/imported/fail/nll/df-same-deep-path.expected
- было (other): `already mutably borrowed`
- стало (E0499/E0502 borrow): `cannot borrow 'n.m.l.x' as mutable more than once at a time`

## tests/imported/fail/nll/df-same-field-twice-mut.expected
- было (other): `already mutably`
- стало (E0499/E0502 borrow): `cannot borrow 'p.a' as mutable more than once at a time`

## tests/imported/fail/nll/df-shared-mut-same-field.expected
- было (other): `already borrowed`
- стало (E0499/E0502 borrow): `cannot borrow 'p.x' as mutable because it is also borrowed as immutable`

## tests/imported/fail/nll/df-whole-then-field.expected
- было (other): `already mutably`
- стало (E0499/E0502 borrow): `cannot borrow 'p' as mutable more than once at a time`

## tests/logos/fail/adv2_two_iter_mut.expected
- было (other): `already mutably borrowed`
- стало (E0499/E0502 borrow): `cannot borrow 'v' as mutable more than once at a time`

## tests/logos/fail/bc_capret_mut_reborrow_of_capture_fail.expected
- было (other): `error [fn f]: captured variable 't' cannot escape `FnMut` closure body: a mutable reborrow of a capture is bounded by the closure call`
- стало (E0499/E0502 borrow): `cannot borrow 'c' as mutable, as it is not declared as mutable`

## tests/logos/fail/bc_capret_mut_reborrow_thru_call_fail.expected
- было (other): `error [fn f]: captured variable 't' cannot escape `FnMut` closure body: a mutable reborrow of a capture is bounded by the closure call`
- стало (E0499/E0502 borrow): `cannot borrow 'c' as mutable, as it is not declared as mutable`

## tests/logos/fail/core_2_adv_alias_through_ref.expected
- было (other): `already mutably borrowed`
- стало (E0499/E0502 borrow): `cannot borrow 'x' as mutable more than once at a time`

## tests/logos/fail/core_2_adv_double_mut.expected
- было (other): `already mutably borrowed`
- стало (E0499/E0502 borrow): `cannot borrow 'x' as mutable more than once at a time`

## tests/logos/fail/core_6_1_union_borrow_alias.expected
- было (other): `already mutably borrowed`
- стало (E0499/E0502 borrow): `cannot borrow 'u.a' as mutable more than once at a time`

## tests/logos/fail/index_mut_alias.expected
- было (other): `already mutably borrowed`
- стало (E0499/E0502 borrow): `cannot borrow 'v' as mutable more than once at a time`

## tests/spec/fail/borrow_diag_1__take-field-borrow-blocks-whole-mut.expected
- было (other): `field of 'x' is already borrowed`
- стало (E0499/E0502 borrow): `cannot borrow 'x.a' as mutable because it is also borrowed as immutable`

## tests/spec/fail/borrow_diag_2__index-reborrow.expected
- было (other): `already mutably borrowed`
- стало (E0499/E0502 borrow): `cannot borrow 'v' as mutable more than once at a time`

## tests/spec/fail/borrow_diag_2__recv-self-conflict.expected
- было (other): `has shared borrows`
- стало (E0499/E0502 borrow): `cannot borrow 'v' as mutable because it is also borrowed as immutable`

## tests/imported/fail/borrowck/issue-36082.expected
- было (other): `[fn main]: temporary value dropped while borrowed: this reference borrows into a temporary that is dropped at the end of the statement`
- стало (E0505 move): `cannot move out of '__rtmp_0' because it is borrowed`

## tests/imported/fail/borrowck/rvalue-borrow-scope-error.expected
- было (other): `temporary value dropped while borrowed: this reference borrows into a temporary that is dropped at the end of the statement; bind the owning value to a variable first so it outlives the borrow`
- стало (E0505 move): `cannot move out of '__rtmp_0' because it is borrowed`

## tests/imported/fail/drop/if-let-rescope-borrowck-suggestions.expected
- было (other): `temporary value dropped while borrowed: this reference borrows into a temporary that is dropped at the end of the statement; bind the owning value to a variable first so it outlives the borrow`
- стало (E0505 move): `cannot move out of '__rtmp_0' because it is borrowed`

## tests/logos/fail/bc_0914o_autoreffund_hb_i03_refuse.expected
- было (other): `temporary value dropped while borrowed: this reference borrows into a temporary that is dropped at the end of the statement`
- стало (E0505 move): `cannot move out of '__rtmp_0' because it is borrowed`

## tests/logos/fail/bc_0914o_autoreffund_hb_i04_refuse.expected
- было (other): `temporary value dropped while borrowed: this reference borrows into a temporary that is dropped at the end of the statement`
- стало (E0505 move): `cannot move out of '__rtmp_0' because it is borrowed`

## tests/logos/fail/bc_e716argtemp_rtmp_drop_fail.expected
- было (other): `temporary value dropped while borrowed: this reference borrows into a temporary that is dropped at the end of the statement; bind the owning value to a variable first so it outlives the borrow`
- стало (E0505 move): `cannot move out of '__rtmp_0.v' because it is borrowed`

## tests/logos/fail/borrow_temp_dropped_while_borrowed.expected
- было (other): `temporary value dropped while borrowed`
- стало (E0505 move): `cannot move out of '__rtmp_0' because it is borrowed`

## tests/spec/fail/borrow_diag_2__ref-from-temp.expected
- было (other): `temporary value dropped while borrowed`
- стало (E0505 move): `cannot move out of '__rtmp_0' because it is borrowed`

## tests/logos/fail/borrow_struct_held_mut.expected
- было (other): `'f' is already mutably borrowed`
- стало (E0506 assign): `cannot assign to 'f' because it is borrowed`

## tests/logos/fail/assign_ref_dangling.expected
- было (other): `exit: 1`
- стало (E0515 return-ref): `cannot return reference to local variable 'local': dangling reference`

## tests/imported/fail/borrowck/borrowck-borrowed-uniq-rvalue-2.expected
- было (other): `temporary value dropped while borrowed: this reference borrows into a temporary that is dropped at the end of the statement; bind the owning value to a variable first so it outlives the borrow`
- стало (E0597 outlives): `'_t3' does not live long enough: it is borrowed here and used after '_t3' goes out of scope`

## tests/imported/fail/borrowck/issue-11493.expected
- было (other): `temporary value dropped while borrowed: this reference borrows into a temporary that is dropped at the end of the statement; bind the owning value to a variable first so it outlives the borrow`
- стало (E0597 outlives): `'_t3' does not live long enough: it is borrowed here and used after '_t3' goes out of scope`

## tests/imported/fail/borrowck/issue-17545.expected
- было (other): `temporary value dropped while borrowed: this reference borrows into a temporary that is dropped at the end of the statement; bind the owning value to a variable first so it outlives the borrow`
- стало (E0597 outlives): `'_t3' does not live long enough: it is borrowed here and used after '_t3' goes out of scope`

## tests/imported/fail/dropck/dropck-eyepatch-extern-crate.expected
- было (other): `binding 'dt' has a `Drop` impl and borrows local 'c_shortest', but 'c_shortest' goes out of scope before 'dt' is dropped`
- стало (E0597 outlives): `'c_shortest' does not live long enough: it is borrowed here and used after 'c_shortest' goes out of scope`

## tests/imported/fail/dropck/dropck-eyepatch-reorder.expected
- было (other): `binding 'd' has a `Drop` impl and borrows local 'c', but 'c' goes out of scope before 'd' is dropped`
- стало (E0597 outlives): `'c' does not live long enough: it is borrowed here and used after 'c' goes out of scope`

## tests/imported/fail/dropck/dropck-trait-cycle-checked.expected
- было (other): `binding 'h' has a `Drop` impl and borrows local 'x', but 'x' goes out of scope before 'h' is dropped`
- стало (E0597 outlives): `'x' does not live long enough: it is borrowed here and used after 'x' goes out of scope`

## tests/imported/fail/dropck/eager-by-ref-binding-for-guards.expected
- было (other): `binding 'long1' has a `Drop` impl and borrows local 'short1', but 'short1' goes out of scope before 'long1' is dropped`
- стало (E0597 outlives): `'short1' does not live long enough: it is borrowed here and used after 'short1' goes out of scope`

## tests/imported/fail/dropck/let-else-more-permissive.expected
- было (other): `binding 'long1' has a `Drop` impl and borrows local 'short1', but 'short1' goes out of scope before 'long1' is dropped`
- стало (E0597 outlives): `'short1' does not live long enough: it is borrowed here and used after 'short1' goes out of scope`

## tests/imported/fail/lifetimes/temporary-lifetime-extension-tuple-ctor.expected
- было (other): `temporary value dropped while borrowed`
- стало (E0597 outlives): `'_t11' does not live long enough: it is borrowed here and used after '_t11' goes out of scope`

## tests/imported/fail/nll/sugg-mut-for-binding-issue-137486.expected
- было (other): `[fn main]: temporary value dropped while borrowed: 'ref_s' borrows into a temporary that is dropped at the end of the statement and is used here; bind the owning value to a variable first so it outlives the borrow (E0716)`
- стало (E0597 outlives): `'_t3' does not live long enough: it is borrowed here and used after '_t3' goes out of scope`

## tests/imported/fail/regions/dropck-loop-borrow-of-iter-local.expected
- было (other): `Drop`
- стало (E0597 outlives): `'local' does not live long enough: it is borrowed here and used after 'local' goes out of scope`

## tests/imported/fail/regions/dropck-multi-source.expected
- было (other): `Drop`
- стало (E0597 outlives): `'b' does not live long enough: it is borrowed here and used after 'b' goes out of scope`

## tests/imported/fail/regions/dropck-nested-three-deep.expected
- было (other): `Drop`
- стало (E0597 outlives): `'v' does not live long enough: it is borrowed here and used after 'v' goes out of scope`

## tests/imported/fail/regions/dropck-reassign-from-inner.expected
- было (other): `Drop`
- стало (E0597 outlives): `'v1' does not live long enough: it is borrowed here and used after 'v1' goes out of scope`

## tests/imported/fail/regions/dropck-reassign-in-inner.expected
- было (other): `Drop`
- стало (E0597 outlives): `'v1' does not live long enough: it is borrowed here and used after 'v1' goes out of scope`

## tests/imported/fail/regions/dropck-scope-outlives.expected
- было (other): `Drop`
- стало (E0597 outlives): `'v' does not live long enough: it is borrowed here and used after 'v' goes out of scope`

## tests/imported/fail/regions/dropck-three-scope-levels.expected
- было (other): `Drop`
- стало (E0597 outlives): `'mid' does not live long enough: it is borrowed here and used after 'mid' goes out of scope`

## tests/imported/fail/regions/regions-var-type-out-of-scope.expected
- было (other): `[fn foo]: temporary value dropped while borrowed: 'x' borrows into a temporary that is dropped at the end of the statement and is used here; bind the owning value to a variable first so it outlives the borrow (E0716)`
- стало (E0597 outlives): `'_t4' does not live long enough: it is borrowed here and used after '_t4' goes out of scope`

## tests/logos/fail/bc_0912n_bcdoor_hb_i2_refuse.expected
- было (other): `[fn main]: temporary value dropped while borrowed: this reference borrows into a temporary that is dropped at the end of the statement; bind the owning value to a variable first so it outlives the borrow`
- стало (E0597 outlives): `'_t4' does not live long enough: it is borrowed here and used after '_t4' goes out of scope`

## tests/logos/fail/bc_0912n_bcdoor_hb_i3_refuse.expected
- было (other): `[fn main]: temporary value dropped while borrowed: this reference borrows into a temporary that is dropped at the end of the statement; bind the owning value to a variable first so it outlives the borrow`
- стало (E0597 outlives): `'_t5' does not live long enough: it is borrowed here and used after '_t5' goes out of scope`

## tests/logos/fail/bc_0912n_bcdoor_hb_i5_refuse.expected
- было (other): `[fn main]: temporary value dropped while borrowed: this reference borrows into a temporary that is dropped at the end of the statement; bind the owning value to a variable first so it outlives the borrow`
- стало (E0597 outlives): `'_t5' does not live long enough: it is borrowed here and used after '_t5' goes out of scope`

## tests/logos/fail/bc_bcdoor_argborrow_refuse.expected
- было (other): `temporary value dropped while borrowed`
- стало (E0597 outlives): `'_t6' does not live long enough: it is borrowed here and used after '_t6' goes out of scope`

## tests/logos/fail/bc_bcdoor_closure_refuse.expected
- было (other): `temporary value dropped while borrowed`
- стало (E0597 outlives): `'_t5' does not live long enough: it is borrowed here and used after '_t5' goes out of scope`

## tests/logos/fail/bc_bcdoor_fnptr_refuse.expected
- было (other): `temporary value dropped while borrowed`
- стало (E0597 outlives): `'_t4' does not live long enough: it is borrowed here and used after '_t4' goes out of scope`

## tests/logos/fail/bc_dropck_field_two_paths_fail.expected
- было (other): `binding 'w' has a `Drop` impl and borrows local 'inner', but 'inner' goes out of scope before 'w' is dropped`
- стало (E0597 outlives): `'inner' does not live long enough: it is borrowed here and used after 'inner' goes out of scope`

## tests/logos/fail/bc_dropck_field_two_paths_swapped_fail.expected
- было (other): `binding 'w' has a `Drop` impl and borrows local 'inner', but 'inner' goes out of scope before 'w' is dropped`
- стало (E0597 outlives): `'inner' does not live long enough: it is borrowed here and used after 'inner' goes out of scope`

## tests/logos/fail/bc_dropck_reverse_order_fail.expected
- было (other): `binding 'h' has a `Drop` impl and borrows local 'x', but 'x' goes out of scope before 'h' is dropped`
- стало (E0597 outlives): `'x' does not live long enough: it is borrowed here and used after 'x' goes out of scope`

## tests/logos/fail/bc_dropck_source_dies_first_fail.expected
- было (other): `binding 'g' has a `Drop` impl and borrows local 'short', but 'short' goes out of scope before 'g' is dropped`
- стало (E0597 outlives): `'short' does not live long enough: it is borrowed here and used after 'short' goes out of scope`

## tests/logos/fail/bc_e716argtemp_field_hop_fail.expected
- было (other): `temporary value dropped while borrowed: this reference borrows into a temporary that is dropped at the end of the statement; bind the owning value to a variable first so it outlives the borrow`
- стало (E0597 outlives): `'_t3.v' does not live long enough: it is borrowed here and used after '_t3.v' goes out of scope`

## tests/logos/fail/bc_e716recvrefbase_dangle_fail.expected
- было (other): `temporary value dropped while borrowed: this reference borrows into a temporary that is dropped at the end of the statement; bind the owning value to a variable first so it outlives the borrow`
- стало (E0597 outlives): `'_t4' does not live long enough: it is borrowed here and used after '_t4' goes out of scope`

## tests/logos/fail/bc_e716recvtemp_nodrop_fail.expected
- было (other): `temporary value dropped while borrowed: this reference borrows into a temporary that is dropped at the end of the statement; bind the owning value to a variable first so it outlives the borrow`
- стало (E0597 outlives): `'_t2' does not live long enough: it is borrowed here and used after '_t2' goes out of scope`

## tests/logos/fail/bc_let_borrow_temp_through_call_fail.expected
- было (other): `temporary value dropped while borrowed`
- стало (E0597 outlives): `'_t3' does not live long enough: it is borrowed here and used after '_t3' goes out of scope`

## tests/logos/fail/bc_tmpassign_mut_used_after_fail.expected
- было (other): `[fn f]: temporary value dropped while borrowed: 'r' borrows into a temporary that is dropped at the end of the statement and is used here; bind the owning value to a variable first so it outlives the borrow (E0716)`
- стало (E0597 outlives): `'_t3' does not live long enough: it is borrowed here and used after '_t3' goes out of scope`

## tests/logos/fail/bc_tmpassign_used_after_fail.expected
- было (other): `[fn f]: temporary value dropped while borrowed: 'x' borrows into a temporary that is dropped at the end of the statement and is used here; bind the owning value to a variable first so it outlives the borrow (E0716)`
- стало (E0597 outlives): `'_t3' does not live long enough: it is borrowed here and used after '_t3' goes out of scope`

## tests/logos/fail/bcs_temp_enum_let_e0716.expected
- было (other): `temporary value dropped while borrowed: this reference borrows into a temporary that is dropped at the end of the statement; bind the owning value to a variable first so it outlives the borrow`
- стало (E0597 outlives): `'_t5' does not live long enough: it is borrowed here and used after '_t5' goes out of scope`

## tests/logos/fail/bcs_temp_struct_assign_e0716.expected
- было (other): `temporary value dropped while borrowed: this reference borrows into a temporary that is dropped at the end of the statement; bind the owning value to a variable first so it outlives the borrow`
- стало (E0597 outlives): `'_t8' does not live long enough: it is borrowed here and used after '_t8' goes out of scope`

## tests/logos/fail/bcs_temp_struct_let_e0716.expected
- было (other): `temporary value dropped while borrowed: this reference borrows into a temporary that is dropped at the end of the statement; bind the owning value to a variable first so it outlives the borrow`
- стало (E0597 outlives): `'_t9' does not live long enough: it is borrowed here and used after '_t9' goes out of scope`

## tests/logos/fail/nll_call_ref_outlives_scope.expected
- было (other): `exit: 1`
- стало (E0597 outlives): `'x' does not live long enough: it is borrowed here and used after 'x' goes out of scope`

## tests/logos/fail/nll_field_write_outlives_scope.expected
- было (other): `exit: 1`
- стало (E0597 outlives): `'x' does not live long enough: it is borrowed here and used after 'x' goes out of scope`

## tests/logos/fail/nll_match_arm_ref_outlives_scope.expected
- было (other): `exit: 1`
- стало (E0597 outlives): `'x' does not live long enough: it is borrowed here and used after 'x' goes out of scope`

## tests/logos/fail/nll_method_borrow_outlives_scope.expected
- было (other): `exit: 1`
- стало (E0597 outlives): `'b' does not live long enough: it is borrowed here and used after 'b' goes out of scope`

## tests/logos/fail/nll_stored_borrow_outlives_scope.expected
- было (other): `exit: 1`
- стало (E0597 outlives): `'x' does not live long enough: it is borrowed here and used after 'x' goes out of scope`

## tests/logos/fail/nll_struct_ref_outlives_scope.expected
- было (other): `exit: 1`
- стало (E0597 outlives): `'x' does not live long enough: it is borrowed here and used after 'x' goes out of scope`

## tests/logos/fail/nll_vec_ref_outlives_scope.expected
- было (other): `exit: 1`
- стало (E0597 outlives): `'x' does not live long enough: it is borrowed here and used after 'x' goes out of scope`

## tests/spec/fail/borrow_diag_1__dropck-binding-outlive.expected
- было (other): `has a `Drop` impl and borrows local`
- стало (E0597 outlives): `'local' does not live long enough: it is borrowed here and used after 'local' goes out of scope`


## tests/imported/fail/lifetimes/ex2d-push-inference-variable-2.expected
- было (sema variance, at the LEGAL `let a: &mut Vec<Ref> = x`): `let 'a': variance mismatch — expected &mut Vec<Ref>, got &'a mut Vec<Ref<'b>>`
- стало (lifetime may not live long enough, at the push, as rustc): `lifetime may not live long enough: consider adding the bound `'c: 'b``

## tests/imported/fail/nll/issue-52742.expected
- было (E0597, an artefact of the port's single-region `Foo`): `'tmp' does not live long enough …`
- стало (at upstream's own site, `self.y = b.z` in `take_bar`, once upstream's `Foo<'a, 'b>` is restored; rustc: lifetime may not live long enough): `assignment to 'self.y': variance mismatch`

## tests/imported/fail/nll/issue-52059-report-when-borrow-and-drop-conflict.expected
- было (E0509, moved-out-of-Drop — the value was MOVED out at the return): `cannot move out of 's.url': its owner implements Drop (E0509, line 53)`
- стало (E0713, as upstream — a return is a coercion site, so the `&mut` field is REBORROWED and the destructor runs over a live reborrow): `line 53: borrow may still be in use when destructor runs: '*s.url' (E0713)`

## tests/imported/fail/nll/enum-drop-access.expected
- было (E0509, moved-out-of-Drop — the value was MOVED out at the return): `cannot move out of 'opt.r': its owner implements Drop (E0509, line 56)`
- стало (E0713, as upstream — a return is a coercion site, so the `&mut` field is REBORROWED and the destructor runs over a live reborrow): `line 56: borrow may still be in use when destructor runs: '*opt.r' (E0713)`

## tests/imported/fail/nll/issue-53773.expected
- было (move out while borrowed, at the return): `cannot move out of '*child.raw' because it is borrowed`
- стало (E0713, as upstream: the return REBORROWS the `&mut` field and `C`'s destructor runs over it): `borrow may still be in use when destructor runs: '*child.raw' (E0713)`

## #464/#465 drop-while-borrowed wording (17 fixtures)
- было (a DROP reported as a move): `cannot move out of 'X' because it is borrowed`
- стало (as rustc: E0716 for a temporary, E0597 for a named local): `temporary value dropped while borrowed` / `'X' does not live long enough`
  - tests/logos/fail/borrow_temp_dropped_while_borrowed.expected
  - tests/logos/fail/bc_0914o_autoreffund_hb_i03_refuse.expected
  - tests/logos/fail/bc_0914o_autoreffund_hb_i04_refuse.expected
  - tests/logos/fail/bc_e716argtemp_rtmp_drop_fail.expected
  - tests/logos/fail/bc_esc_fnptr_param_dangle.expected
  - tests/logos/fail/bc_esc_generic_outparam_dangle.expected
  - tests/logos/fail/bc_esc_holder_return_method_dangle.expected
  - tests/imported/fail/borrowck/borrowck-borrowed-uniq-rvalue.expected
  - tests/imported/fail/borrowck/borrowck-fn-in-const-c.expected
  - tests/imported/fail/dropck/drop-with-active-borrows-2.expected
  - tests/imported/fail/drop/if-let-rescope-borrowck-suggestions.expected
  - tests/imported/fail/borrowck/issue-36082.expected
  - tests/imported/fail/nll/issue-54556-used-vs-unused-tails.expected
  - tests/imported/fail/lifetimes/return-reference-local-variable-13497.expected
  - tests/imported/fail/borrowck/rvalue-borrow-scope-error.expected
  - tests/logos/fail/wany_escapes_rc_container.expected
  - tests/spec/fail/borrow_diag_2__ref-from-temp.expected
