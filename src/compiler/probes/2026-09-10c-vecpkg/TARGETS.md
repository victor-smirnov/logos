# 2026-09-10c — TARGET SITES, WRITTEN BEFORE THE MEASUREMENT

Subject fixed by the prompt: `struct_name() != "Vec"` at five sites in
`sema_expr.cpp`, no package check at any.

RE-DERIVED (the prompt's line numbers are a hypothesis, rule 17). On `827dc89b0`
the five are **19028 / 19035 / 20067 / 20078 / 22097** — the prompt says
19023/19030/20062/20073/22092, five lines earlier, because `827dc89b0` added
`bound_is_deref_lang_item`'s call above them. The COUNT and the ORDER are right;
the numbers are stale by one commit. A mechanical replace of the exact literal
`if (TypeRef(t).struct_name() != "Vec") return false;` hit **exactly 5**
occurrences, which is the class as a set, not as a line list.

| # | line | lambda | what it recognises |
|---|---|---|---|
| S1 | 19028 | `is_vec_ident_qi` | `Vec<Ident>` cursor, **quote_item!** path |
| S2 | 19035 | `is_vec_exprblob_qi` | `Vec<ExprBlob>` cursor, quote_item! path |
| S3 | 20067 | `is_vec_ident_type` | `Vec<Ident>` cursor, **quote_expr!** path |
| S4 | 20078 | `is_vec_exprblob_type` | `Vec<ExprBlob>` cursor, quote_expr! path |
| S5 | 22097 | `is_vec_exprblob` | **`#[fn_macro]` signature (b)** `(Vec<ExprBlob>) -> ExprBlob` |

## WHY THIS BLOCK AND NOT ANOTHER

* The payload of every one of the five is ALREADY package-qualified —
  `is_ident` / `is_exprblob` in `sema_impl.hpp` carry
  `pkg_name() == "logos.std.compiler.metaprog"` (task #99). The CONTAINER is the
  only unqualified operand left in the predicate. That is the "arm that exists,
  reached through a fact the code does not carry" shape, and the fact is carried
  three lines away in the same expression.
* The idiom to copy is `is_stdlib_box` (`sema_impl.hpp`, called from
  `mono_impl.hpp:922`): bare name + `pkg.empty() || pkg == "<owner>"`.
* `Vec` is a TYPE, not a trait, so `bound_is_*_lang_item` does NOT transfer;
  measured rather than assumed.

## THE THREE RECORDED CONTROLS RE-VERIFIED FIRST

`827dc89b0`'s record DECLINED this block on "0 wrong verdicts in 2 shapes" and
stated its own limit: CE9 was inconclusive because the doors are in SERIES and
only the OUTER one was observed. Both of its shapes are re-run here as v1/v2/v6
and both still reproduce its verdict. The declension is what this round tests.
