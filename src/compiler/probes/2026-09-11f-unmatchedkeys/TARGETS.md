# TARGET KEYS, NAMED BEFORE THE MEASUREMENT WAS INTERPRETED

Subject fixed by the prompt: the layout verifier's `unmatched` answers. The
prompt's three-plus-floor was re-derived, NOT inherited (rule 17). The prompt
says `option_box_recursive_struct_field_list` reports **3**; on build hash
`8c2294bf39c2e079 43` it reports **5** — that number is one commit stale
(`acd150787` landed between). The floor of 2 reproduces exactly.

Target keys (by id), the five of `option_box_recursive_struct_field_list`:

  T1  mono_abi_layout  c-like   logos.lang.cmp.Ordering                       (the floor)
  T2  sema_abi_layout  tagged   logos.lang.option.Option$G1$Location          (the floor)
  T3  sema_abi_layout  niche    logos.lang.option.Option$G1$Box$G1$Node
  T4  sema_abi_layout  tagged   logos.lang.result.Result$G2$void$Error
  T5  sema_abi_layout  product  logos.std.compiler.metaprog.Ident$M2b09c0fe11e753e9

WHY THIS BLOCK OVER THE QUEUE'S 82 ROWS: the subject is assigned, and the block
is the one shape the prompt says has paid every time — an ARM THAT EXISTS
(`truth[key]`, the `llvm::DataLayout` row, already built in the same compile)
reached through a fact the code does not carry (that sema's key and mlir-gen's
key NAME THE SAME TYPE). Nothing new has to be computed; only the identity has
to be carried.

GROUPING TEST (does ONE candidate change move BOTH members?):
  T2/T3/T4 — YES, one root: the `$G<n>$` vs `__` spelling of a generic ENUM
             instance.
  T5       — NO: a `$M<16hex>` metaprog/hygiene suffix present on sema's key and
             absent from mlir-gen's. Separate root.
  T1       — NO: neither spelling; mono names it `<pkg>.<bare>` and the only
             `.Ordering` row in `truth` belongs to a DIFFERENT enum
             (`logos.lang.atomic.Ordering`). Separate root.
So: five keys, THREE roots. Both halves of the handed-down "floor of two" are in
different roots.
