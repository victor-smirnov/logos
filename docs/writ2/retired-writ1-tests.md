# Writ1 test adaptation log (step 6 of the writ2 cut-over)

UPDATE 2026-06-11: per Victor's direction, NO tests were retired — all 78
Writ1-era tests were ADAPTED to writ2 equivalents preserving their
coverage intent. The adaptation built these writ2 surfaces:

- mem.writ2.equal / hashing / clone / check — direct HAny walkers (the
  stringify foundation pattern; the Writ1 tag_dispatch tables stay retired).
- mem.writ2.parser — extended with bare-ident keys, typed dense containers
  (<T>[...] / <K[,AnyVal]>{...} incl strictness rules), @Type(params)?=init
  prefix + "init"@Type(params) postfix typed values with @Decimal(P,S)
  validation; stringify renders the canonical postfix form.
- lang.writ2.decimal — hdec_from_str / hdec_cmp / hdec_add on the
  u64-coefficient model (the limb bignum is gone; overflow is an error).
- lang.writ2.typed_arr — MapSlice* + writ2_build_map_*_anyval revive the
  `as <K,AnyVal>{}` casts on value-form &[HAny] (Refs re-anchor via clone).
- lang.fabric.string_store — the SoA string storage (Buffer<StrDt>); bare
  scalars ride the Primitive blanket (Buffer<i64>).
- mem.writ2.tag_system — h2_tag_size; registry lookups verified live.

Semantic shifts (writ2 model differences, intentional):
- parse errors -> null handles (the lenient parser has no Result channel);
- clone IS compactify (live-set copy);
- holder model: Zone/DataRef/DataOwn -> Rc<Writ2> + Held<T> + RAII;
- HBS wire = the mem.writ2.hbs codec (tag-by-tag), import = clone_into.
