# Hermes1 test adaptation log (step 6 of the hermes2 cut-over)

UPDATE 2026-06-11: per Victor's direction, NO tests were retired — all 78
Hermes1-era tests were ADAPTED to hermes2 equivalents preserving their
coverage intent. The adaptation built these hermes2 surfaces:

- mem.hermes2.equal / hashing / clone / check — direct HAny walkers (the
  stringify foundation pattern; the Hermes1 tag_dispatch tables stay retired).
- mem.hermes2.parser — extended with bare-ident keys, typed dense containers
  (<T>[...] / <K[,AnyVal]>{...} incl strictness rules), @Type(params)?=init
  prefix + "init"@Type(params) postfix typed values with @Decimal(P,S)
  validation; stringify renders the canonical postfix form.
- lang.hermes2.decimal — hdec_from_str / hdec_cmp / hdec_add on the
  u64-coefficient model (the limb bignum is gone; overflow is an error).
- lang.hermes2.typed_arr — MapSlice* + hermes2_build_map_*_anyval revive the
  `as <K,AnyVal>{}` casts on value-form &[HAny] (Refs re-anchor via clone).
- lang.fabric.string_store — the SoA string storage (Buffer<StrDt>); bare
  scalars ride the Primitive blanket (Buffer<i64>).
- mem.hermes2.tag_system — h2_tag_size; registry lookups verified live.

Semantic shifts (hermes2 model differences, intentional):
- parse errors -> null handles (the lenient parser has no Result channel);
- clone IS compactify (live-set copy);
- holder model: Zone/DataRef/DataOwn -> Rc<Hermes2> + Held<T> + RAII;
- HBS wire = the mem.hermes2.hbs codec (tag-by-tag), import = clone_into.
