// Logos project — https://github.com/victor-smirnov/logos
//
// mono_subst.cpp — Type substitution for the monomorphization pass.

#include "mono_impl.hpp"

namespace logos::compiler {

// Phase 5.B step 3: thin wrapper around TypePool::intern_foreign so all
// "pull a foreign TypeRef into our local pool" calls have a uniform name
// at the call site. Recursion lives on TypePool itself (so non-Mono
// callers — notably lir_mirror's type_av — can use it without threading
// Mono through every emit boundary).
TypeRef Mono::localize_type(TypeRef tv) noexcept {
    return out_.type_pool.intern_foreign(tv);
}

TypeRef Mono::subst_type(TypeRef tv, const SubstMap& s) noexcept {
    if (!tv) return tv;
    // Phase 5.B step 3 NOTE: deliberately NO eager localize here. Eager
    // localize eats >1s on iterator-heavy compiles because every
    // recursive subst_type call would deep-copy types that are then
    // either thrown away or re-substituted. Foreign offsets are caught
    // at the write boundary instead: lir_mirror's type_av calls
    // intern_foreign so anything stored into a local mirror's TYPE
    // field is local by construction. Reads through foreign TypeRefs
    // are safe — lir_view's accessors are cross-arena-aware.
    if (tv.kind() == LogosType::Kind::TypeVar || tv.kind() == LogosType::Kind::ConstVar) {
        auto it = s.find(std::string(tv.type_var_name()));
        if (it != s.end()) return it->second;
        return tv;
    }
    if (tv.kind() == LogosType::Kind::Array) {
        auto elem = subst_type(tv.elem(), s);
        uint64_t size = tv.arr_size();
        std::string symbolic = std::string(tv.arr_size_var());
        if (!symbolic.empty()) {
            // [T; sizeof...(P)] sema lowers to arr_size_var "__sizeof_pack:P".
            // At mono, `cur_packs_` holds the concrete expansion of P — emit
            // its length as the literal size.
            constexpr std::string_view PFX = "__sizeof_pack:";
            if (symbolic.compare(0, PFX.size(), PFX) == 0) {
                std::string pname = symbolic.substr(PFX.size());
                auto pit = cur_packs_.find(pname);
                if (pit != cur_packs_.end()) {
                    size = (uint64_t)pit->second.size();
                    symbolic = "";
                }
            } else {
                auto it = s.find(symbolic);
                if (it != s.end()) {
                    TypeRef itv{it->second};
                    if (itv.const_val()) {
                        size = (uint64_t)*itv.const_val();
                        symbolic = ""; // Resolved to literal
                    } else if (itv.kind() == LogosType::Kind::ConstVar) {
                        symbolic = std::string(itv.type_var_name()); // Still symbolic
                    }
                }
            }
        }
        if (elem == tv.elem() && size == tv.arr_size() && symbolic == tv.arr_size_var()) return tv;
        LogosTypeBuilder nt = tv.to_builder();
        nt.elem = elem;
        nt.arr_size = size;
        nt.arr_size_var = symbolic;
        return out_.type_pool.alloc(nt);
    }
    switch (tv.kind()) {
    case LogosType::Kind::Ptr:
    case LogosType::Kind::Ref:
    case LogosType::Kind::MutRef: {
        auto inner = subst_type(tv.pointee(), s);
        // Phase 1B-2: when a `T: ?Sized` substitution lands an UnsizedSlice<U>
        // inside `&T` / `&mut T` / `*const T` / `*mut T`, canonicalise to
        // the existing Kind::Slice (fat-pointer ABI). Mirrors the sema-side
        // subst_type_sema canonicalisation; mono operates over the same
        // type-pool but with its own allocator/recorder.
        if (inner && inner.kind() == LogosType::Kind::UnsizedSlice) {
            LogosTypeBuilder snt; snt.kind = LogosType::Kind::Slice;
            snt.elem = inner.elem();
            return out_.type_pool.alloc(std::move(snt));
        }
        // Phase 1B-4: same canonicalisation for UnsizedDyn → TraitObject.
        if (inner && inner.kind() == LogosType::Kind::UnsizedDyn) {
            LogosTypeBuilder tnt; tnt.kind = LogosType::Kind::TraitObject;
            tnt.trait_name = std::string(inner.trait_name());
            tnt.type_args = inner.type_args();
            return out_.type_pool.alloc(std::move(tnt));
        }
        // Phase 1B-14: when substitution lands a custom-DST struct as the
        // pointee (is_dst on its LStructDef), canonicalise to DstRef.
        // Mirror of sema_decl's resolve_type canonicalisation, applied at
        // mono substitution time when T binds to a DST struct.
        if (inner && (inner.kind() == LogosType::Kind::Struct ||
                      inner.kind() == LogosType::Kind::ZonedStruct)) {
            std::string sn(inner.struct_name());
            // Phase 1B-14: trust the template's is_dst flag. For generic
            // DST instantiations (post-subst last field unsized), sema's
            // canonicalisation already produced DstRef at type-resolution
            // time; we don't repeat the recursive check here (it would
            // re-walk the struct's field type, potentially deep into
            // self-referential generic types).
            // M2: bare-first lookup preserves prior behavior for this
            // specific call (DST flag is pkg-independent in practice, but
            // bare-first is the historical ordering here).
            const TypePoolImpl* mst_pool = out_.type_pool.impl();
            auto sit_ptr = find_struct_template_bare_first(inner.pkg_name(), sn);
            // Per-instance DST: the template is NOT flagged is_dst (its tail is
            // a generic `T`, not a literal `[U]`), but THIS instantiation bound
            // that tail param to an unsized type — `Inner<dyn Tr>` /
            // `RcInner<dyn Tr>`. The instance is then a custom-DST and `*mut/&`
            // to it is a fat pointer. Detect SHALLOWLY (no recursion — a field
            // reached through a pointer is itself sized): the template's last
            // field is exactly the tail type-param, and this instance's
            // corresponding type-arg is unsized (UnsizedDyn / UnsizedSlice).
            // Without this, a generic method's `self.inner.field` baked a thin
            // GEP at sema-lower time and reads the fat pointer's data half as
            // the field → garbage. Mirrors sema is_effective_dst at mono time.
            bool inst_dst = false;
            if (sit_ptr.valid() && !sit_ptr.is_dst() && !sit_ptr.fields().empty() &&
                !sit_ptr.type_params_empty()) {
                auto sit_fields = sit_ptr.fields();
                auto sit_tps = sit_ptr.type_params();
                TypeRef lastf = sit_fields.back().type(mst_pool);
                auto args = inner.type_args();
                if (lastf && lastf.kind() == LogosType::Kind::TypeVar) {
                    std::string tvn(lastf.type_var_name());
                    for (size_t i = 0; i < sit_tps.size() &&
                                       i < args.size(); ++i) {
                        if (sit_tps[i].name() == tvn) {
                            auto ak = args[i].kind();
                            // TraitObject = a `dyn Trait` arg canonicalised to
                            // the uniform fat form (the common case for
                            // `Arc<dyn>`/`Rc<dyn>`, esp. cross-module). Bound to
                            // the bare tail param it still makes the instance a
                            // custom-DST → `*mut/&` to it is FAT. Mirror of
                            // sema is_effective_dst. (A `&[T]`/Slice arg is a
                            // SIZED fat-ptr value, not an unsized tail — excluded.)
                            // A TraitObject arg counts ONLY when it's a BARE
                            // `dyn` (owning kind Borrow — the unsized object). An
                            // owning `Box<dyn>`/`Rc<dyn>`/`Arc<dyn>` arg (owning
                            // Box/Rc/Arc) is a SIZED fat handle VALUE held inline
                            // (e.g. `HashMap`'s entry value), NOT an unsized tail.
                            inst_dst = (ak == LogosType::Kind::UnsizedDyn ||
                                        ak == LogosType::Kind::UnsizedSlice ||
                                        (ak == LogosType::Kind::TraitObject &&
                                         TypeRef(args[i]).trait_owning_kind() ==
                                             TypeRef::OwningKind::Borrow));
                            break;
                        }
                    }
                }
            }
            // Non-generic custom-DST (`*mut Foo`, Foo has a `[u8]`/`dyn` tail):
            // struct_templates_ is generics-only, so sit_ptr is null here —
            // consult the all-structs index so a non-generic DST pointee
            // canonicalises to DstRef too (matches sema's PTR_TYPE resolve;
            // prevents the field-repr divergence that left it thin Ptr).
            auto any_sit = find_any_struct(inner.pkg_name(), sn);
            bool tmpl_dst = (sit_ptr.valid() && sit_ptr.is_dst()) ||
                            (any_sit.valid() && any_sit.is_dst());
            // Hermes / RefRepr: a `#[self_describing]` DST recovers its tail
            // length from an in-band prefix field, so a RAW `*const/*mut Self`
            // stays THIN (kind=Ptr, 8B) — do NOT canonicalise to fat DstRef.
            // `&Self` / `&mut Self` keep the fat repr (no in-band len contract),
            // so the skip is gated on the pointer being a raw Ptr.
            bool self_desc = (sit_ptr.valid() && sit_ptr.self_describing()) ||
                             (any_sit.valid() && any_sit.self_describing());
            if (self_desc && tv.kind() == LogosType::Kind::Ptr) {
                if (inner == tv.pointee()) return tv;
                LogosTypeBuilder nt = tv.to_builder(); nt.pointee = inner;
                return out_.type_pool.alloc(nt);
            }
            if (tmpl_dst || inst_dst) {
                LogosTypeBuilder dn; dn.kind = LogosType::Kind::DstRef;
                dn.struct_name = sn;
                dn.pkg_name = std::string(inner.pkg_name());
                dn.mut_ptr = (tv.kind() == LogosType::Kind::MutRef) ||
                             (tv.kind() == LogosType::Kind::Ptr && tv.mut_ptr());
                dn.type_args = inner.type_args();
                return out_.type_pool.alloc(std::move(dn));
            }
        }
        if (inner == tv.pointee()) return tv;
        LogosTypeBuilder nt = tv.to_builder(); nt.pointee = inner;
        return out_.type_pool.alloc(nt);
    }
    case LogosType::Kind::Struct:
    case LogosType::Kind::ZonedStruct: {
        if (tv.type_args().empty()) return tv;
        std::vector<TypeRef> new_args;
        bool changed = false;
        for (auto a : tv.type_args()) {
            auto na = subst_type(a, s);
            changed |= (na != a);
            new_args.push_back(na);
        }
        if (!changed) return tv;
        LogosTypeBuilder nt = tv.to_builder();
        nt.type_args = std::move(new_args);
        // Track this instantiation for struct monomorphization.
        TypeRef result = out_.type_pool.alloc(nt);
        record_needed_struct(result);
        return result;
    }
    case LogosType::Kind::Enum: {
        if (tv.type_args().empty()) return tv;
        std::vector<TypeRef> new_args;
        bool changed = false;
        for (auto a : tv.type_args()) {
            auto na = subst_type(a, s);
            changed |= (na != a);
            new_args.push_back(na);
        }
        if (!changed) {
            // Still record the need even if types didn't change
            // (e.g., non-generic function using Option<i32>).
            record_needed_enum(tv);
            return tv;
        }
        LogosTypeBuilder nt; nt.kind = LogosType::Kind::Enum;
        nt.enum_name = std::string(tv.enum_name());
        nt.pkg_name  = std::string(tv.pkg_name());  // preserve pkg through subst
        nt.type_args = std::move(new_args);
        TypeRef result = out_.type_pool.alloc(std::move(nt));
        record_needed_enum(result);
        return result;
    }
    case LogosType::Kind::Slice: {
        auto elem = subst_type(tv.elem(), s);
        if (elem == tv.elem()) return tv;
        LogosTypeBuilder nt; nt.kind = LogosType::Kind::Slice;
        nt.elem = elem;
        return out_.type_pool.alloc(std::move(nt));
    }
    case LogosType::Kind::UnsizedSlice: {
        // Phase 1B-2: substitute the element. The UnsizedSlice kind survives
        // here only when it appears outside a `&` / `*` wrapper (caught by
        // the Ptr/Ref/MutRef canonicalisation above). At MLIR-gen time
        // reaching this kind is a sentinel for an unsized value position.
        auto elem = subst_type(tv.elem(), s);
        if (elem == tv.elem()) return tv;
        LogosTypeBuilder nt; nt.kind = LogosType::Kind::UnsizedSlice;
        nt.elem = elem;
        return out_.type_pool.alloc(std::move(nt));
    }
    case LogosType::Kind::UnsizedDyn: {
        // Phase 1B-4: substitute trait type args. Same survival pattern as
        // UnsizedSlice — only reachable outside a `&` / `*` wrapper.
        if (tv.type_args().empty()) return tv;
        std::vector<TypeRef> new_args;
        bool changed = false;
        for (auto a : tv.type_args()) {
            auto na = subst_type(a, s);
            changed |= (na != a);
            new_args.push_back(na);
        }
        if (!changed) return tv;
        LogosTypeBuilder nt; nt.kind = LogosType::Kind::UnsizedDyn;
        nt.trait_name = std::string(tv.trait_name());
        nt.type_args = std::move(new_args);
        return out_.type_pool.alloc(std::move(nt));
    }
    case LogosType::Kind::DstRef: {
        // Phase 1B-15: substitute type-args so DstRef<Wrap<T>> can be
        // monomorphised through outer scopes.
        if (tv.type_args().empty()) return tv;
        std::vector<TypeRef> new_args;
        bool changed = false;
        for (auto a : tv.type_args()) {
            auto na = subst_type(a, s);
            changed |= (na != a);
            new_args.push_back(na);
        }
        if (!changed) return tv;
        LogosTypeBuilder dn; dn.kind = LogosType::Kind::DstRef;
        dn.struct_name = std::string(tv.struct_name());
        dn.pkg_name = std::string(tv.pkg_name());
        dn.mut_ptr = tv.mut_ptr();
        dn.type_args = std::move(new_args);
        return out_.type_pool.alloc(std::move(dn));
    }
    case LogosType::Kind::TraitObject: {
        if (tv.type_args().empty()) return tv;
        std::vector<TypeRef> new_args;
        bool changed = false;
        for (auto a : tv.type_args()) {
            auto na = subst_type(a, s);
            changed |= (na != a);
            new_args.push_back(na);
        }
        if (!changed) return tv;
        LogosTypeBuilder nt = tv.to_builder();
        nt.type_args = std::move(new_args);
        return out_.type_pool.alloc(std::move(nt));
    }
    case LogosType::Kind::Tuple: {
        // Variadic-tuple pack expansion (Phase 4 step 3, mono mirror of
        // sema's subst_type_sema fast-path): `Tuple<[TypeVar(A)]>` where
        // A maps to a concrete Tuple → splice the concrete tuple's
        // elements in place. Lets `&(A...)` substitute to `&(T1, T2, ...)`
        // inside cloned variadic-tuple impl method bodies.
        auto orig_elems = tv.tuple_elems();
        if (orig_elems.size() == 1 && orig_elems[0] &&
            TypeRef(orig_elems[0]).kind() == LogosType::Kind::TypeVar) {
            std::string tv_name(TypeRef(orig_elems[0]).type_var_name());
            auto it = s.find(tv_name);
            if (it != s.end()) {
                TypeRef mapped(it->second);
                if (mapped.kind() == LogosType::Kind::Tuple) {
                    std::vector<TypeRef> new_elems;
                    for (auto e : mapped.tuple_elems())
                        new_elems.push_back(subst_type(e, s));
                    LogosTypeBuilder nt; nt.kind = LogosType::Kind::Tuple;
                    nt.tuple_elems = std::move(new_elems);
                    return out_.type_pool.alloc(std::move(nt));
                }
            }
            // Also check cur_packs_ — variadic fn-param convention
            // stores pack contents under the pack name. A → [T1, T2, ...].
            auto pit = cur_packs_.find(tv_name);
            if (pit != cur_packs_.end()) {
                std::vector<TypeRef> new_elems;
                for (auto e : pit->second)
                    new_elems.push_back(subst_type(e, s));
                LogosTypeBuilder nt; nt.kind = LogosType::Kind::Tuple;
                nt.tuple_elems = std::move(new_elems);
                return out_.type_pool.alloc(std::move(nt));
            }
        }
        std::vector<TypeRef> new_elems;
        bool changed = false;
        for (auto e : tv.tuple_elems()) {
            auto ne = subst_type(e, s);
            changed |= (ne != e);
            new_elems.push_back(ne);
        }
        if (!changed) return tv;
        LogosTypeBuilder nt; nt.kind = LogosType::Kind::Tuple;
        nt.tuple_elems = std::move(new_elems);
        return out_.type_pool.alloc(std::move(nt));
    }
    case LogosType::Kind::FnItem:
    case LogosType::Kind::FnPtr:
    case LogosType::Kind::Closure: {
        // Substitute fn-ptr / closure signatures: `fn(T, U) -> V` carries
        // its argument list and return type in closure_params/closure_ret
        // (same slots as Closure). When T/U/V are TypeVars bound by the
        // surrounding generic, recurse into them; otherwise pass through.
        std::vector<TypeRef> new_params;
        bool changed = false;
        for (auto p : tv.closure_params()) {
            auto np = subst_type(p, s);
            changed |= (np != p);
            new_params.push_back(np);
        }
        TypeRef new_ret = subst_type(tv.closure_ret(), s);
        changed |= (new_ret != tv.closure_ret());
        if (!changed) return tv;
        LogosTypeBuilder nt = tv.to_builder();
        nt.closure_params = std::move(new_params);
        nt.closure_ret    = new_ret;
        return out_.type_pool.alloc(std::move(nt));
    }
    case LogosType::Kind::AssocType: {
        // Resolve: recursively substitute the base, then look up TraitName::ConcreteType::AssocName
        auto subbed_base = subst_type(tv.assoc_base(), s);
        TypeRef sbv{subbed_base};
        // Scalar kinds (u64/i32/bool/...) — concrete_base is the type's
        // canonical name. Lets bare scalars resolve assoc types via the
        // Primitive→Container blanket chain in stdlib.
        bool scalar_base = false;
        switch (sbv.kind()) {
            case LogosType::Kind::Bool:
            case LogosType::Kind::I8:  case LogosType::Kind::I16:
            case LogosType::Kind::I32: case LogosType::Kind::I64:
            case LogosType::Kind::U8:  case LogosType::Kind::U16:
            case LogosType::Kind::U32: case LogosType::Kind::U64:
            case LogosType::Kind::F32: case LogosType::Kind::F64:
                scalar_base = true; break;
            default: break;
        }
        if (sbv.kind() == LogosType::Kind::Struct ||
            sbv.kind() == LogosType::Kind::ZonedStruct ||
            sbv.kind() == LogosType::Kind::Enum ||
            scalar_base) {
            std::string concrete_base;
            if (sbv.kind() == LogosType::Kind::Struct ||
                sbv.kind() == LogosType::Kind::ZonedStruct)
                concrete_base = concrete_struct_name(subbed_base);
            else if (sbv.kind() == LogosType::Kind::Enum)
                concrete_base = std::string(sbv.enum_name());
            else
                concrete_base = type_str(subbed_base);

            std::string key = std::string(tv.trait_name()) + "::" + concrete_base + "::" + std::string(tv.assoc_type_name());
            auto ait = assoc_impls_.find(key);
            if (ait != assoc_impls_.end()) {
                // Collapse nested associated-type chains fully
                return subst_type(ait->second, {});
            }
            // Blanket fallback: when there's an `impl<T: Bound> Trait for T`
            // and `concrete_base` satisfies Bound, use the blanket's assoc.
            for (auto& bi : blanket_impls_) {
                if (bi.trait_name != tv.trait_name()) continue;
                StrSet seen_pri;
                if (!bi.bound_trait.empty() &&
                    !mono_has_impl_recursive(bi.bound_trait, concrete_base, seen_pri)) continue;
                bool all_extra = true;
                for (auto& eb : bi.extra_bounds) {
                    StrSet seen_eb;
                    if (!mono_has_impl_recursive(eb, concrete_base, seen_eb)) {
                        all_extra = false; break;
                    }
                }
                if (!all_extra) continue;
                auto bait = bi.assoc_types.find(std::string(tv.assoc_type_name()));
                if (bait == bi.assoc_types.end()) continue;
                SubstMap bsubst;
                bsubst[bi.target_typevar] = subbed_base;
                return subst_type(bait->second, bsubst);
            }
        }
        if (subbed_base != tv.assoc_base()) {
            LogosTypeBuilder nt = tv.to_builder();
            nt.assoc_base = subbed_base;
            return out_.type_pool.alloc(std::move(nt));
        }
        return tv;
    }
    case LogosType::Kind::CfgSlotType: {
        // <type:CFG.path> — extract the type stored at the given path of
        // HermesStatic-bound CFG. CFG can be a const-generic param
        // (resolves through `s`) or a type alias to an HStaticLit (already
        // a concrete bound when type aliases are inlined). When CFG is not
        // yet concrete, stay deferred.
        //
        // The path is encoded in `assoc_type_name` (one entry per step,
        // each as `kind_byte + payload`, joined by 0x1F):
        //   'F' + name   — string-keyed map field
        //   'I' + intstr — integer-keyed map field
        //   'A' + intstr — array index
        std::string cfg_name = std::string(tv.type_var_name());
        std::string path_enc = std::string(tv.assoc_type_name());
        TypeRef cfg = nullptr;
        auto sit = s.find(cfg_name);
        if (sit != s.end()) cfg = sit->second;
        if (cfg) cfg = subst_type(cfg, s);
        if (!cfg || TypeRef(cfg).kind() != LogosType::Kind::HStaticLit) return tv;
        uint64_t hash = (uint64_t)cfg.const_val().value_or(0);
        auto rav = out_.hstatic_registry_.get(std::to_string(hash));
        if (rav.is_null()) return tv;
        lir_view::ExprRef eref(out_.type_pool.arena(), rav);
        if (eref.addr() == nullptr) return tv;
        if (eref.kind() != lir_schema::expr::Code::HermesLit) return tv;
        // Decode path.
        struct Step { char kind; std::string name; int64_t index; };
        std::vector<Step> steps;
        {
            size_t p = 0;
            while (p < path_enc.size()) {
                Step st{};
                st.kind = path_enc[p++];
                size_t e = path_enc.find('\x1F', p);
                if (e == std::string::npos) e = path_enc.size();
                std::string payload = path_enc.substr(p, e - p);
                if (st.kind == 'F') st.name = std::move(payload);
                else st.index = std::stoll(payload);
                steps.push_back(std::move(st));
                p = e + 1;
            }
        }
        // Walk path through the Hermes value.
        lir_view::HermesValRef cur = lir_view::EHermesLitView{eref}.root();
        for (auto& st : steps) {
            using K = lir_schema::hermes_val::Code;
            bool found = false;
            if (st.kind == 'F' || st.kind == 'I') {
                if (cur.kind() != K::Map) return tv;
                auto map = lir_view::HVMapView{cur};
                if (st.kind == 'F' && !map.int_keyed()) {
                    for (uint64_t i = 0, n = map.size(); i < n; ++i)
                        if (map.str_key(i) == st.name) { cur = map.value(i); found = true; break; }
                } else if (st.kind == 'I' && map.int_keyed()) {
                    for (uint64_t i = 0, n = map.size(); i < n; ++i)
                        if (map.int_key(i) == st.index) { cur = map.value(i); found = true; break; }
                }
            } else if (st.kind == 'A') {
                if (cur.kind() != K::Array) return tv;
                auto arr = lir_view::HVArrayView{cur};
                if ((uint64_t)st.index >= arr.size()) return tv;
                cur = arr.elem((uint64_t)st.index);
                found = true;
            }
            if (!found) return tv;
        }
        if (cur.kind() == lir_schema::hermes_val::Code::Type) {
            std::string tname(lir_view::HVTypeView{cur}.name());
            auto alloc_kind = [&](LogosType::Kind k) -> TypeRef {
                LogosTypeBuilder b; b.kind = k;
                return out_.type_pool.alloc(std::move(b));
            };
            if (tname == "u8")   return alloc_kind(LogosType::Kind::U8);
            if (tname == "u16")  return alloc_kind(LogosType::Kind::U16);
            if (tname == "u32")  return alloc_kind(LogosType::Kind::U32);
            if (tname == "u64")  return alloc_kind(LogosType::Kind::U64);
            if (tname == "i8")   return alloc_kind(LogosType::Kind::I8);
            if (tname == "i16")  return alloc_kind(LogosType::Kind::I16);
            if (tname == "i32")  return alloc_kind(LogosType::Kind::I32);
            if (tname == "i64")  return alloc_kind(LogosType::Kind::I64);
            if (tname == "f32")  return alloc_kind(LogosType::Kind::F32);
            if (tname == "f64")  return alloc_kind(LogosType::Kind::F64);
            if (tname == "bool") return alloc_kind(LogosType::Kind::Bool);
            for (auto& sd : out_.structs)
                if (sd.name() == tname) {
                    LogosTypeBuilder b;
                    b.kind = sd.is_zoned() ? LogosType::Kind::ZonedStruct
                                         : LogosType::Kind::Struct;
                    b.struct_name = tname;
                    b.pkg_name = std::string(sd.pkg());
                    return out_.type_pool.alloc(std::move(b));
                }
            for (auto& ed : out_.enums)
                if (ed.name() == tname) {
                    LogosTypeBuilder b;
                    b.kind = LogosType::Kind::Enum;
                    b.enum_name = tname;
                    b.pkg_name = std::string(ed.pkg());
                    return out_.type_pool.alloc(std::move(b));
                }
            return tv;
        }
        return tv;
    }
    default:
        return tv;
    }
}

} // namespace logos::compiler
