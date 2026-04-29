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
