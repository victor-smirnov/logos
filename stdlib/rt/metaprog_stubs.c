// Stubs for metaprog host symbols when linking a non-JIT binary.
//
// The Logos compiler's metaprog stdlib (std.compiler.metaprog —
// Emitter, OView, error_at, etc.) externs these symbols. The host
// compiler binary defines real impls in main.cpp and binds them at
// JIT-link time. After hooks have run, the user's compiled artifact
// strips the handler bodies (see main.cpp's "strip metaprog hook fns
// from the FINAL prog" pass), but the *imported* stdlib fns
// (Emitter::commit, oview_module_ast, OView::drop, error_at) still
// land in the user object as dead code — they're non-generic, package-
// level, and the current emit pass keeps them.
//
// Until the emitter learns to drop unreferenced stdlib fns, these
// stubs satisfy the linker. They abort if ever called, because that
// would mean a non-handler is invoking metaprog machinery in the
// runtime — a bug.

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

static void __attribute__((noreturn)) metaprog_unavailable(const char* fn) {
    fprintf(stderr,
        "logos: metaprog runtime symbol '%s' invoked outside the compiler\n",
        fn);
    abort();
}

__attribute__((weak))
int32_t logos_emit_source(const char* src) {
    (void)src;
    metaprog_unavailable("logos_emit_source");
}

__attribute__((weak))
int32_t logos_emit_item_blob(const uint8_t* data, uint64_t size) {
    (void)data; (void)size;
    metaprog_unavailable("logos_emit_item_blob");
}

__attribute__((weak))
int32_t logos_metaprog_test_module_blob(const char* src, uint64_t src_len,
                                        const uint8_t** out_data,
                                        uint64_t* out_size) {
    (void)src; (void)src_len; (void)out_data; (void)out_size;
    metaprog_unavailable("logos_metaprog_test_module_blob");
}

__attribute__((weak))
void logos_get_module_ast_oview(void** out_holder,
                                uint64_t* out_root_offset,
                                const uint8_t** out_zone_base) {
    (void)out_holder; (void)out_root_offset; (void)out_zone_base;
    metaprog_unavailable("logos_get_module_ast_oview");
}

__attribute__((weak))
void logos_holder_release(void* h) {
    (void)h;
    // Tolerate a null-holder release path without aborting: hooks
    // running outside metaprog mode never get a real holder, so the
    // RC drop on an OView whose holder field is 0 is a normal exit.
    if (h == 0) return;
    metaprog_unavailable("logos_holder_release");
}

__attribute__((weak))
void logos_metaprog_error(const char* msg) {
    (void)msg;
    metaprog_unavailable("logos_metaprog_error");
}

__attribute__((weak))
void logos_metaprog_error_at(uint32_t target_offset, const char* msg) {
    (void)target_offset; (void)msg;
    metaprog_unavailable("logos_metaprog_error_at");
}

__attribute__((weak))
const uint8_t* logos_test_make_bin_op_blob(void) {
    metaprog_unavailable("logos_test_make_bin_op_blob");
}

__attribute__((weak))
const uint8_t* logos_quote_expr_subst(const uint8_t* tpl, uint64_t tpl_size,
                                      const void* idents_ptr,
                                      uint64_t idents_count) {
    (void)tpl; (void)tpl_size; (void)idents_ptr; (void)idents_count;
    metaprog_unavailable("logos_quote_expr_subst");
}

__attribute__((weak))
const uint8_t* logos_qib_pack_idents(const void* const* arr, uint64_t n) {
    (void)arr; (void)n;
    metaprog_unavailable("logos_qib_pack_idents");
}

__attribute__((weak))
void logos_qib_free_idents(const uint8_t* blob) {
    (void)blob;
    metaprog_unavailable("logos_qib_free_idents");
}

__attribute__((weak))
const uint8_t* logos_qib_pack_blobs(const uint8_t* const* arr, uint64_t n) {
    (void)arr; (void)n;
    metaprog_unavailable("logos_qib_pack_blobs");
}

__attribute__((weak))
void logos_qib_free_blobs(const uint8_t* blob) {
    (void)blob;
    metaprog_unavailable("logos_qib_free_blobs");
}

__attribute__((weak))
const uint8_t* logos_metaprog_gensym(const uint8_t* pref, uint64_t pref_len,
                                     uint64_t* out_len) {
    (void)pref; (void)pref_len; (void)out_len;
    metaprog_unavailable("logos_metaprog_gensym");
}

__attribute__((weak))
const uint8_t* logos_macro_arg(uint64_t site_id, uint64_t arg_idx) {
    (void)site_id; (void)arg_idx;
    metaprog_unavailable("logos_macro_arg");
}

// Rule-IR handoff for deem/mapping item thunks (ADR 0016). The std layer's
// own deem items (logos.std.canon.canon, ADR 0020) put __metacall_thunk_*
// bodies referencing this into liblogos-std.a — dead in a compiled binary,
// but the linker still wants the symbol.
__attribute__((weak))
const uint8_t* logos_rule_ir(uint64_t site_id) {
    (void)site_id;
    metaprog_unavailable("logos_rule_ir");
}

__attribute__((weak))
int32_t logos_emit_item_blob_subst(const void* blob) {
    (void)blob;
    metaprog_unavailable("logos_emit_item_blob_subst");
}

__attribute__((weak))
const uint8_t* logos_qib_pack_cursors(const void* const* arr, uint64_t n) {
    (void)arr; (void)n;
    metaprog_unavailable("logos_qib_pack_cursors");
}

__attribute__((weak))
void logos_qib_free_cursors(const uint8_t* blob) {
    (void)blob;
    metaprog_unavailable("logos_qib_free_cursors");
}

__attribute__((weak))
void logos_get_module_ast(const uint8_t** out_base, uint64_t* out_size) {
    (void)out_base; (void)out_size;
    metaprog_unavailable("logos_get_module_ast");
}

__attribute__((weak))
const uint8_t* logos_metacall_freeze2(uint64_t root_word) {
    (void)root_word;
    metaprog_unavailable("logos_metacall_freeze2");
}

// ── Fiber-runtime stubs for the metacall JIT ────────────────────────
//
// liblstdlib_fibers.a (fiber_ctx.S) carries TLS relocations
// (R_X86_64_GOTTPOFF) that ORC's RuntimeDyld can't resolve, so it's
// filtered out of the metacall JIT's archive set. Metaprog handlers
// never actually run on a Logos fiber — they execute on the host's
// main thread — but their compiled bodies may transitively reference
// these symbols through stdlib types (Vec, String, …) that have
// fiber-aware paths. Weak no-op stubs here let the JIT link without
// pulling in the real, non-relocatable archive.
//
// At user-link time these stubs are overridden by the strong
// definitions in liblstdlib_fibers.a.

__attribute__((weak))
void logos_fiber_switch(void* from, const void* to) {
    (void)from; (void)to;
    metaprog_unavailable("logos_fiber_switch");
}

__attribute__((weak))
void* logos_get_fiber_entry_addr(void) {
    metaprog_unavailable("logos_get_fiber_entry_addr");
}

__attribute__((weak))
void logos_fiber_entry(void) {
    metaprog_unavailable("logos_fiber_entry");
}

// Pointer accessors: the real fiber_ctx.S uses TLS via GOTTPOFF; the
// stubs hand back a single non-TLS slot — handlers don't share fiber
// state, so a single global is fine for the JIT-only path. (At user
// link time fiber_ctx.S's strong definitions override these.)
// MUST NOT be __thread here: TLS relocations would make this .o
// unloadable by ORC RuntimeDyld too, putting us back at square one.
static void* _stub_slot;

__attribute__((weak)) void* logos_sched_get(void) { return _stub_slot; }
__attribute__((weak)) void  logos_sched_set(void* p) { _stub_slot = p; }
__attribute__((weak)) void* logos_thread_get(void) { return _stub_slot; }
__attribute__((weak)) void  logos_thread_set(void* p) { _stub_slot = p; }
__attribute__((weak)) void* logos_reactor_get(void) { return _stub_slot; }
__attribute__((weak)) void  logos_reactor_set(void* p) { _stub_slot = p; }
__attribute__((weak)) int64_t* logos_user_tls_get(void) { return (int64_t*)&_stub_slot; }
__attribute__((weak)) void  logos_user_tls_set(int64_t* p) { (void)p; }

// Process-global cross-thread fiber-wake hook (fiber_ctx.S .data slot). The
// std thread pool installs its waker here; lcm's future_complete reads it.
// JIT-only stub uses a dedicated non-TLS slot; fiber_ctx.S overrides at link.
static int64_t _stub_xwake;
__attribute__((weak)) int64_t logos_xwake_get(void) { return _stub_xwake; }
__attribute__((weak)) void  logos_xwake_set(int64_t p) { _stub_xwake = p; }

// Fiber stack provisioning (logos_fiber_stack_alloc/free) lives in fiber_stack.c
// and the monotonic clock (logos_clock_now) in clock.c — strong implementations
// in this same rt archive. No weak stub is needed for either: they are always
// resolvable for both the metacall JIT and user links (unlike logos_xwake_*,
// whose strong def sits in the separately-filtered fibers archive). An LCM
// target that does not link liblstdlib_rt.a provides its own.
