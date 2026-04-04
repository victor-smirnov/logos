// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos

#include <logos/hermes/template.hpp>
#include <logos/hermes/access.hpp>
#include <logos/hermes/path.hpp>
#include <logos/hermes/named_code.hpp>
#include <logos/hermes/stringify.hpp>
#include <logos/hermes/arena_string.hpp>
#include <logos/hermes/arena_value.hpp>
#include <logos/hermes/object_array.hpp>
#include <logos/hermes/object_map.hpp>
#include <logos/hermes/tiny_object_map.hpp>
#include <logos/core/err.hpp>

#include <string>
#include <vector>

namespace logos::hermes {

// ============================================================================
// Template AST codes (stored in TinyObjectMap key 0)
// ============================================================================

namespace tpl_ast {
    using Key  = NamedCode<uint8_t>;
    using Code = NamedCode<int32_t>;

    // Field keys (stored in TinyObjectMap).
    inline constexpr Key CODE              {"CODE",         0};
    inline constexpr Key EXPRESSION        {"EXPRESSION",   1};
    inline constexpr Key VARIABLE          {"VARIABLE",     2};
    inline constexpr Key STATEMENTS        {"STATEMENTS",   3};
    inline constexpr Key ELSE_BRANCH       {"ELSE_BRANCH",  4};
    inline constexpr Key STRIP_BEFORE      {"STRIP_BEFORE", 5};
    inline constexpr Key STRIP_AFTER       {"STRIP_AFTER",  6};

    // Node types (stored as value of key CODE).
    inline constexpr Code TEXT_NODE    {"TEXT_NODE",  0};
    inline constexpr Code VAR_STMT     {"VAR_STMT",   1};
    inline constexpr Code FOR_STMT     {"FOR_STMT",   2};
    inline constexpr Code IF_STMT      {"IF_STMT",    3};
    inline constexpr Code ELSE_STMT    {"ELSE_STMT",  4};
    inline constexpr Code SET_STMT     {"SET_STMT",   5};
}

// ============================================================================
// Template Parser
// ============================================================================

class TemplateParser {
public:
    TemplateParser(std::string_view text, HermesCtr& doc)
        : text_(text), pos_(0), doc_(doc) {}

    void* parse() {
        auto* stmts = HermesCtrAccess::raw_array(doc_).get();
        parse_block(stmts, "");
        return stmts;
    }

private:
    std::string_view text_;
    size_t pos_;
    HermesCtr& doc_;

    bool at_end() const { return pos_ >= text_.size(); }

    // Parse a block of text + statements until we hit an end tag or EOF.
    // end_tag: "endfor", "endif", "else", "elif", or "" for top-level.
    // Returns the keyword that terminated the block.
    std::string parse_block(ObjectArray* stmts, std::string_view end_tag) {
        while (!at_end()) {
            // Scan for {{ or {%
            size_t text_start = pos_;
            while (pos_ < text_.size()) {
                if (text_[pos_] == '{' && pos_ + 1 < text_.size()) {
                    char next = text_[pos_ + 1];
                    if (next == '{' || next == '%') break;
                }
                ++pos_;
            }

            // Emit text node if any.
            if (pos_ > text_start) {
                auto sv = text_.substr(text_start, pos_ - text_start);
                auto* s = HermesCtrAccess::raw_string(doc_,sv).get();
                stmts->push_back(AnyVal{}, HermesCtrAccess::arena(doc_)).get();
                stmts->slot(stmts->size() - 1, HermesCtrAccess::base(doc_))->set_pointer(s, HermesCtrAccess::base(doc_));
            }

            if (at_end()) break;

            if (text_[pos_ + 1] == '{') {
                // Variable output: {{ expr }}
                parse_var_stmt(stmts);
            } else {
                // Statement: {% ... %}
                std::string keyword = parse_statement(stmts, end_tag);
                if (!keyword.empty()) return keyword;
            }
        }
        return "";
    }

    void parse_var_stmt(ObjectArray* stmts) {
        pos_ += 2; // skip {{
        skip_ws();
        size_t expr_start = pos_;
        // Find closing }}
        while (pos_ + 1 < text_.size() && !(text_[pos_] == '}' && text_[pos_ + 1] == '}'))
            ++pos_;
        std::string_view expr_text = text_.substr(expr_start, pos_ - expr_start);
        // Trim trailing whitespace from expression.
        while (!expr_text.empty() && (expr_text.back() == ' ' || expr_text.back() == '\t'))
            expr_text.remove_suffix(1);

        if (pos_ + 1 < text_.size()) pos_ += 2; // skip }}

        // Parse the expression as HermesPath AST.
        // Called from within a noexcept barrier — .get() throws on error,
        // which the barrier catches and converts to logos::expected.
        auto expr_doc = parse_path(expr_text).get();

        auto* expr_str = HermesCtrAccess::raw_string(doc_,expr_text).get();
        auto* node = HermesCtrAccess::raw_tiny_map(doc_,4).get();
        node->put(tpl_ast::CODE, AnyVal::from_value(tpl_ast::VAR_STMT), HermesCtrAccess::arena(doc_)).get();
        node->put(tpl_ast::EXPRESSION, AnyVal{}, HermesCtrAccess::arena(doc_)).get();
        node->slot(tpl_ast::EXPRESSION, HermesCtrAccess::base(doc_))->set_pointer(expr_str, HermesCtrAccess::base(doc_));

        stmts->push_back(AnyVal{}, HermesCtrAccess::arena(doc_)).get();
        stmts->slot(stmts->size() - 1, HermesCtrAccess::base(doc_))->set_pointer(node, HermesCtrAccess::base(doc_));
    }

    // Returns non-empty string if this statement is a block-terminating keyword
    // (endfor, endif, else, elif).
    std::string parse_statement(ObjectArray* stmts, std::string_view end_tag) {
        pos_ += 2; // skip {%
        bool strip_before = false;
        if (pos_ < text_.size() && text_[pos_] == '-') { strip_before = true; ++pos_; }
        if (pos_ < text_.size() && text_[pos_] == '+') { ++pos_; } // preserve (default)

        skip_ws();
        std::string keyword = read_keyword();

        if (keyword == "endfor" || keyword == "endif") {
            skip_to_close_tag();
            return keyword;
        }
        if (keyword == "else") {
            skip_to_close_tag();
            return "else";
        }
        if (keyword == "elif") {
            // Don't consume — let the caller re-parse.
            // Back up to before the keyword.
            pos_ -= keyword.size();
            // Actually, return "elif" and let the if-handler deal with it.
            pos_ += keyword.size();
            // Read the elif expression.
            skip_ws();
            std::string_view elif_expr = read_until_close_tag();
            skip_to_close_tag();
            // Store elif expression for the caller.
            elif_expr_ = elif_expr;
            return "elif";
        }

        if (keyword == "for") {
            parse_for_stmt(stmts, strip_before);
            return "";
        }
        if (keyword == "if") {
            parse_if_stmt(stmts, strip_before);
            return "";
        }
        if (keyword == "set") {
            parse_set_stmt(stmts);
            return "";
        }

        // Unknown statement — skip.
        skip_to_close_tag();
        return "";
    }

    void parse_for_stmt(ObjectArray* stmts, bool /*strip*/) {
        skip_ws();
        std::string var_name = read_keyword();
        skip_ws();
        std::string in_kw = read_keyword();
        if (in_kw != "in") throw logos::err(hermes::ErrCode::template_error);
        skip_ws();
        std::string_view expr_text = read_until_close_tag();
        skip_to_close_tag();

        // Parse body.
        auto* body = HermesCtrAccess::raw_array(doc_).get();
        std::string end_kw = parse_block(body, "endfor");
        if (end_kw != "endfor") throw logos::err(hermes::ErrCode::template_error);

        // Allocate strings first, then build the node.
        // All allocations happen before set_pointer to avoid any ordering issues.
        auto* var_str = HermesCtrAccess::raw_string(doc_,var_name).get();
        auto* expr_str = HermesCtrAccess::raw_string(doc_,expr_text).get();

        auto* node = HermesCtrAccess::raw_tiny_map(doc_,8).get();
        node->put(tpl_ast::CODE, AnyVal::from_value(tpl_ast::FOR_STMT), HermesCtrAccess::arena(doc_)).get();
        node->put(tpl_ast::VARIABLE, AnyVal{}, HermesCtrAccess::arena(doc_)).get();
        node->put(tpl_ast::EXPRESSION, AnyVal{}, HermesCtrAccess::arena(doc_)).get();
        node->put(tpl_ast::STATEMENTS, AnyVal{}, HermesCtrAccess::arena(doc_)).get();
        // Now set pointers — all allocations are done, no more arena mutations.
        node->slot(tpl_ast::VARIABLE, HermesCtrAccess::base(doc_))->set_pointer(var_str, HermesCtrAccess::base(doc_));
        node->slot(tpl_ast::EXPRESSION, HermesCtrAccess::base(doc_))->set_pointer(expr_str, HermesCtrAccess::base(doc_));
        node->slot(tpl_ast::STATEMENTS, HermesCtrAccess::base(doc_))->set_pointer(body, HermesCtrAccess::base(doc_));

        stmts->push_back(AnyVal{}, HermesCtrAccess::arena(doc_)).get();
        stmts->slot(stmts->size() - 1, HermesCtrAccess::base(doc_))->set_pointer(node, HermesCtrAccess::base(doc_));
    }

    void parse_if_stmt(ObjectArray* stmts, bool /*strip*/) {
        skip_ws();
        std::string_view expr_text = read_until_close_tag();
        skip_to_close_tag();

        auto* body = HermesCtrAccess::raw_array(doc_).get();
        std::string end_kw = parse_block(body, "endif");

        auto* expr_str = HermesCtrAccess::raw_string(doc_,expr_text).get();
        auto* node = HermesCtrAccess::raw_tiny_map(doc_,8).get();
        node->put(tpl_ast::CODE, AnyVal::from_value(tpl_ast::IF_STMT), HermesCtrAccess::arena(doc_)).get();
        node->put(tpl_ast::EXPRESSION, AnyVal{}, HermesCtrAccess::arena(doc_)).get();
        node->put(tpl_ast::STATEMENTS, AnyVal{}, HermesCtrAccess::arena(doc_)).get();
        node->slot(tpl_ast::EXPRESSION, HermesCtrAccess::base(doc_))->set_pointer(expr_str, HermesCtrAccess::base(doc_));
        node->slot(tpl_ast::STATEMENTS, HermesCtrAccess::base(doc_))->set_pointer(body, HermesCtrAccess::base(doc_));

        // Handle else/elif chain.
        if (end_kw == "else") {
            auto* else_body = HermesCtrAccess::raw_array(doc_).get();
            parse_block(else_body, "endif");
            auto* else_node = HermesCtrAccess::raw_tiny_map(doc_,4).get();
            else_node->put(tpl_ast::CODE, AnyVal::from_value(tpl_ast::ELSE_STMT), HermesCtrAccess::arena(doc_)).get();
            else_node->put(tpl_ast::STATEMENTS, AnyVal{}, HermesCtrAccess::arena(doc_)).get();
            else_node->slot(tpl_ast::STATEMENTS, HermesCtrAccess::base(doc_))->set_pointer(else_body, HermesCtrAccess::base(doc_));
            node->put(tpl_ast::ELSE_BRANCH, AnyVal{}, HermesCtrAccess::arena(doc_)).get();
            node->slot(tpl_ast::ELSE_BRANCH, HermesCtrAccess::base(doc_))->set_pointer(else_node, HermesCtrAccess::base(doc_));
        } else if (end_kw == "elif") {
            // Recursive: build a nested if from the elif.
            auto* elif_stmts = HermesCtrAccess::raw_array(doc_).get();
            parse_if_stmt_with_expr(elif_stmts, elif_expr_);
            if (elif_stmts->size() > 0) {
                node->put(tpl_ast::ELSE_BRANCH, AnyVal{}, HermesCtrAccess::arena(doc_)).get();
                node->slot(tpl_ast::ELSE_BRANCH, HermesCtrAccess::base(doc_))->set_pointer(
                    elif_stmts->slot(0, HermesCtrAccess::base(doc_))->as_ptr<void>(HermesCtrAccess::base(doc_)), HermesCtrAccess::base(doc_));
            }
        }

        stmts->push_back(AnyVal{}, HermesCtrAccess::arena(doc_)).get();
        stmts->slot(stmts->size() - 1, HermesCtrAccess::base(doc_))->set_pointer(node, HermesCtrAccess::base(doc_));
    }

    void parse_if_stmt_with_expr(ObjectArray* stmts, std::string_view expr_text) {
        auto* body = HermesCtrAccess::raw_array(doc_).get();
        std::string end_kw = parse_block(body, "endif");

        auto* expr_str = HermesCtrAccess::raw_string(doc_,expr_text).get();
        auto* node = HermesCtrAccess::raw_tiny_map(doc_,8).get();
        node->put(tpl_ast::CODE, AnyVal::from_value(tpl_ast::IF_STMT), HermesCtrAccess::arena(doc_)).get();
        node->put(tpl_ast::EXPRESSION, AnyVal{}, HermesCtrAccess::arena(doc_)).get();
        node->put(tpl_ast::STATEMENTS, AnyVal{}, HermesCtrAccess::arena(doc_)).get();
        node->slot(tpl_ast::EXPRESSION, HermesCtrAccess::base(doc_))->set_pointer(expr_str, HermesCtrAccess::base(doc_));
        node->slot(tpl_ast::STATEMENTS, HermesCtrAccess::base(doc_))->set_pointer(body, HermesCtrAccess::base(doc_));

        if (end_kw == "else") {
            auto* else_body = HermesCtrAccess::raw_array(doc_).get();
            parse_block(else_body, "endif");
            auto* else_node = HermesCtrAccess::raw_tiny_map(doc_,4).get();
            else_node->put(tpl_ast::CODE, AnyVal::from_value(tpl_ast::ELSE_STMT), HermesCtrAccess::arena(doc_)).get();
            else_node->put(tpl_ast::STATEMENTS, AnyVal{}, HermesCtrAccess::arena(doc_)).get();
            else_node->slot(tpl_ast::STATEMENTS, HermesCtrAccess::base(doc_))->set_pointer(else_body, HermesCtrAccess::base(doc_));
            node->put(tpl_ast::ELSE_BRANCH, AnyVal{}, HermesCtrAccess::arena(doc_)).get();
            node->slot(tpl_ast::ELSE_BRANCH, HermesCtrAccess::base(doc_))->set_pointer(else_node, HermesCtrAccess::base(doc_));
        } else if (end_kw == "elif") {
            auto* elif_stmts = HermesCtrAccess::raw_array(doc_).get();
            parse_if_stmt_with_expr(elif_stmts, elif_expr_);
            if (elif_stmts->size() > 0) {
                node->put(tpl_ast::ELSE_BRANCH, AnyVal{}, HermesCtrAccess::arena(doc_)).get();
                node->slot(tpl_ast::ELSE_BRANCH, HermesCtrAccess::base(doc_))->set_pointer(
                    elif_stmts->slot(0, HermesCtrAccess::base(doc_))->as_ptr<void>(HermesCtrAccess::base(doc_)), HermesCtrAccess::base(doc_));
            }
        }

        stmts->push_back(AnyVal{}, HermesCtrAccess::arena(doc_)).get();
        stmts->slot(stmts->size() - 1, HermesCtrAccess::base(doc_))->set_pointer(node, HermesCtrAccess::base(doc_));
    }

    void parse_set_stmt(ObjectArray* stmts) {
        skip_ws();
        std::string var_name = read_keyword();
        skip_ws();
        if (pos_ < text_.size() && text_[pos_] == '=') ++pos_;
        skip_ws();
        std::string_view expr_text = read_until_close_tag();
        skip_to_close_tag();

        auto* var_str = HermesCtrAccess::raw_string(doc_,var_name).get();
        auto* expr_str = HermesCtrAccess::raw_string(doc_,expr_text).get();
        auto* node = HermesCtrAccess::raw_tiny_map(doc_,4).get();
        node->put(tpl_ast::CODE, AnyVal::from_value(tpl_ast::SET_STMT), HermesCtrAccess::arena(doc_)).get();
        node->put(tpl_ast::VARIABLE, AnyVal{}, HermesCtrAccess::arena(doc_)).get();
        node->put(tpl_ast::EXPRESSION, AnyVal{}, HermesCtrAccess::arena(doc_)).get();
        node->slot(tpl_ast::VARIABLE, HermesCtrAccess::base(doc_))->set_pointer(var_str, HermesCtrAccess::base(doc_));
        node->slot(tpl_ast::EXPRESSION, HermesCtrAccess::base(doc_))->set_pointer(expr_str, HermesCtrAccess::base(doc_));

        stmts->push_back(AnyVal{}, HermesCtrAccess::arena(doc_)).get();
        stmts->slot(stmts->size() - 1, HermesCtrAccess::base(doc_))->set_pointer(node, HermesCtrAccess::base(doc_));
    }

    // --- Lexer helpers ---

    void skip_ws() {
        while (pos_ < text_.size() && (text_[pos_] == ' ' || text_[pos_] == '\t')) ++pos_;
    }

    std::string read_keyword() {
        size_t start = pos_;
        while (pos_ < text_.size() && ((text_[pos_] >= 'a' && text_[pos_] <= 'z') ||
               (text_[pos_] >= 'A' && text_[pos_] <= 'Z') || text_[pos_] == '_' ||
               (text_[pos_] >= '0' && text_[pos_] <= '9')))
            ++pos_;
        return std::string(text_.substr(start, pos_ - start));
    }

    std::string_view read_until_close_tag() {
        size_t start = pos_;
        while (pos_ + 1 < text_.size()) {
            if ((text_[pos_] == '%' || text_[pos_] == '-' || text_[pos_] == '+') &&
                text_[pos_ + 1] == '}') break;
            if (text_[pos_] == '%' && text_[pos_ + 1] == '}') break;
            ++pos_;
        }
        size_t end = pos_;
        // Trim trailing whitespace.
        while (end > start && (text_[end - 1] == ' ' || text_[end - 1] == '\t')) --end;
        return text_.substr(start, end - start);
    }

    void skip_to_close_tag() {
        while (pos_ + 1 < text_.size()) {
            if (text_[pos_] == '%' && text_[pos_ + 1] == '}') { pos_ += 2; return; }
            if (text_[pos_] == '-' && text_[pos_ + 1] == '}') { pos_ += 2; return; }
            if (text_[pos_] == '+' && text_[pos_ + 1] == '}') { pos_ += 2; return; }
            ++pos_;
        }
    }

    std::string_view elif_expr_; // Temporary storage for elif expression.
};

// ============================================================================
// Template Renderer
// ============================================================================

class TemplateRenderer {
public:
    TemplateRenderer(const HermesCtr& data)
        : data_(data) {}

    std::string render(const ObjectArray* stmts, uint8_t* tpl_base) {
        base_ = tpl_base;  // Template AST base for reading nodes.
        visit_stmts(stmts);
        return std::move(out_);
    }

private:
    const HermesCtr& data_;
    std::string out_;
    uint8_t* base_ = nullptr;  // Template AST arena base.
    HermesCtr scratch_ = make_doc_multi(4096).get(); // Scratch arena for materialized values.

    // Variable scope stack.
    struct VarBinding {
        std::string name;
        void* value;
        uint8_t* base;  // arena base of value
    };
    std::vector<VarBinding> var_stack_;

    void push_var(const std::string& name, void* val, uint8_t* val_base = nullptr) {
        var_stack_.push_back({name, val, val_base});
    }

    void pop_vars_to(size_t mark) {
        var_stack_.resize(mark);
    }

    void* find_var(std::string_view name) {
        // Search stack in reverse (most recent first).
        for (auto it = var_stack_.rbegin(); it != var_stack_.rend(); ++it) {
            if (it->name == name) return it->value;
        }
        return nullptr;
    }

    // Evaluate a HermesPath expression against current context.
    // Build a fresh context doc with data + template variables.
    HermesCtr build_context() {
        auto ctx_doc = make_doc_multi().get();
        auto* ctx = HermesCtrAccess::raw_object_map(ctx_doc).get();
        HermesCtrAccess::set_root(ctx_doc, ctx);

        // Copy data fields.
        void* data_root = HermesCtrAccess::root<void>(data_);
        if (data_root) {
            auto* db = static_cast<const uint8_t*>(data_root);
            TypeTag tag = TypeTag::read_before(db);
            if (tag.descriptor() == TagDescriptor::Map && tag.type_code() == type_hash::ObjectMap) {
                auto* data_map = static_cast<ObjectMap*>(data_root);
                uint8_t* data_base = const_cast<uint8_t*>(HermesCtrAccess::base(data_));
                data_map->for_each([&](ArenaString* key, AnyVal* val) {
                    if (val->is_value()) {
                        ctx->put(key->view(), *val, HermesCtrAccess::arena(ctx_doc)).get();
                    } else if (val->is_pointer()) {
                        put_into_ctx(ctx, ctx_doc, key->view(), val->as_ptr<void>(data_base), data_base);
                    }
                }, data_base);
            }
        }

        // Overlay template variables.
        for (auto& binding : var_stack_) {
            if (!binding.value) continue;
            put_into_ctx(ctx, ctx_doc, binding.name, binding.value, binding.base);
        }

        return ctx_doc;
    }

    void* eval_expr(std::string_view expr_text) {
        auto ctx_doc = build_context();
        // Called inside a noexcept barrier — .get() propagates Err via throw.
        auto result = eval_path(ctx_doc, expr_text).get();
        if (!result.has_root()) return nullptr;
        return HermesCtrAccess::root<void>(result);
    }

    std::string eval_to_string(std::string_view expr_text) {
        auto ctx_doc = build_context();
        auto res = eval_path(ctx_doc, expr_text).get();
        if (!res.has_root()) return "";
        return value_to_string(HermesCtrAccess::root<void>(res));
    }

    std::string value_to_string(void* val) {
        if (!val) return "";
        auto* b = static_cast<const uint8_t*>(val);
        TypeTag tag = TypeTag::read_before(b);
        switch (tag.type_code()) {
            case type_hash::Varchar: return std::string(static_cast<const ArenaString*>(val)->view());
            case type_hash::Integer: return std::to_string(*static_cast<const int32_t*>(val));
            case type_hash::BigInt:  return std::to_string(*static_cast<const int64_t*>(val));
            case type_hash::Real: {
                char buf[32];
                int n = std::snprintf(buf, sizeof(buf), "%g", *static_cast<const float*>(val));
                return std::string(buf, n);
            }
            case type_hash::Double: {
                char buf[32];
                int n = std::snprintf(buf, sizeof(buf), "%g", *static_cast<const double*>(val));
                return std::string(buf, n);
            }
            case type_hash::Boolean:
                return *static_cast<const uint8_t*>(val) ? "true" : "false";
            default: return "";
        }
    }

    // Copy data root fields into a context ObjectMap.
    // Put an arena object into context map, copying if needed to avoid cross-arena pointers.
    // NOTE: ctx may be invalidated after this call (arena growth). Re-derive via offset.
    void put_into_ctx(ObjectMap* ctx, HermesCtr& ctx_doc, std::string_view key, void* val,
                     uint8_t* val_base = nullptr) {
        if (!val) { ctx->put(key, AnyVal{}, HermesCtrAccess::arena(ctx_doc)).get(); return; }
        auto* b = static_cast<const uint8_t*>(val);
        TypeTag tag = TypeTag::read_before(b);
        uint64_t tc = tag.type_code();
        // Embeddable scalars: store as embedded AnyVal (no pointer needed).
        switch (tc) {
            case type_hash::Integer:
                ctx->put(key, AnyVal::from_value(*static_cast<const int32_t*>(val), tc), HermesCtrAccess::arena(ctx_doc)).get();
                return;
            case type_hash::UInteger:
                ctx->put(key, AnyVal::from_value(*static_cast<const uint32_t*>(val), tc), HermesCtrAccess::arena(ctx_doc)).get();
                return;
            case type_hash::Boolean:
                ctx->put(key, AnyVal::from_value(*static_cast<const uint8_t*>(val), tc), HermesCtrAccess::arena(ctx_doc)).get();
                return;
            case type_hash::Real:
                ctx->put(key, AnyVal::from_value(*static_cast<const float*>(val), tc), HermesCtrAccess::arena(ctx_doc)).get();
                return;
            case type_hash::SmallInt:
                ctx->put(key, AnyVal::from_value(*static_cast<const int16_t*>(val), tc), HermesCtrAccess::arena(ctx_doc)).get();
                return;
            case type_hash::TinyInt:
                ctx->put(key, AnyVal::from_value(*static_cast<const int8_t*>(val), tc), HermesCtrAccess::arena(ctx_doc)).get();
                return;
            case type_hash::Varchar: {
                auto* s = static_cast<const ArenaString*>(val);
                auto* copy = ArenaString::create(HermesCtrAccess::arena(ctx_doc), s->view()).get();
                uint8_t* cb = HermesCtrAccess::base(ctx_doc);
                ctx->put(key, AnyVal{}, HermesCtrAccess::arena(ctx_doc)).get();
                ctx->get_slot(key, cb)->set_pointer(copy, cb);
                return;
            }
            default: {
                // Complex types (arrays, maps): deep-copy into ctx_doc so offsets are consistent.
                uint8_t* vb = val_base ? val_base : const_cast<uint8_t*>(HermesCtrAccess::base(data_));
                void* copy = copy_object_into(val, vb, ctx_doc).get();
                uint8_t* cb = HermesCtrAccess::base(ctx_doc);
                ctx->put(key, AnyVal{}, HermesCtrAccess::arena(ctx_doc)).get();
                ctx->get_slot(key, cb)->set_pointer(copy, cb);
                return;
            }
        }
    }

    // Materialize an embedded AnyVal value into an arena-allocated object.
    // Uses a scratch doc so the pointer stays valid for the template's lifetime.
    void* materialize_embedded(AnyVal* slot) {
        if (!slot || slot->is_null()) return nullptr;
        uint8_t th = slot->value_type_hash();
        switch (th) {
            case type_hash::Integer:
                return HermesCtrAccess::make_value<int32_t>(scratch_, slot->as_value<int32_t>());
            case type_hash::UInteger:
                return HermesCtrAccess::make_value<uint32_t>(scratch_, slot->as_value<uint32_t>());
            case type_hash::Boolean: {
                TypeTag tag(type_hash::Boolean, TagDescriptor::Data);
                void* mem = HermesCtrAccess::arena(scratch_).allocate(1, 2, tag).get();
                *static_cast<uint8_t*>(mem) = slot->as_value<uint8_t>();
                return mem;
            }
            case type_hash::Real:
                return HermesCtrAccess::make_value<float>(scratch_, slot->as_value<float>());
            case type_hash::SmallInt:
                return HermesCtrAccess::make_value<int16_t>(scratch_, slot->as_value<int16_t>());
            case type_hash::TinyInt:
                return HermesCtrAccess::make_value<int8_t>(scratch_, slot->as_value<int8_t>());
            default: return nullptr;
        }
    }

    bool is_truthy(void* val) {
        if (!val) return false;
        auto* b = static_cast<const uint8_t*>(val);
        TypeTag tag = TypeTag::read_before(b);
        uint64_t tc = tag.type_code();
        if (tc == type_hash::Boolean) return *static_cast<const uint8_t*>(val) != 0;
        if (tc == type_hash::Integer) return *static_cast<const int32_t*>(val) != 0;
        if (tc == type_hash::Varchar) return static_cast<const ArenaString*>(val)->length() > 0;
        if (tag.descriptor() == TagDescriptor::Array) return static_cast<const ObjectArray*>(val)->size() > 0;
        return true;
    }

    // --- Visit methods ---

    void visit_stmts(const ObjectArray* stmts) {
        auto* stmts_mut = const_cast<ObjectArray*>(stmts);
        for (uint64_t i = 0; i < stmts->size(); ++i) {
            AnyVal* slot = stmts_mut->slot(i, base_);
            if (slot->is_null()) continue;
            if (!slot->is_pointer()) continue;
            void* elem = slot->as_ptr<void>(base_);
            auto* eb = static_cast<const uint8_t*>(elem);
            TypeTag tag = TypeTag::read_before(eb);

            if (tag.type_code() == type_hash::Varchar) {
                // Text node.
                out_ += static_cast<const ArenaString*>(elem)->view();
            } else if (tag.descriptor() == TagDescriptor::Map && tag.type_code() == type_hash::Hermes) {
                // Statement node (TinyObjectMap).
                auto* node = static_cast<TinyObjectMap*>(elem);
                int32_t code = node->get(tpl_ast::CODE, base_).as_value<int32_t>();
                switch (code) {
                    case tpl_ast::VAR_STMT:  visit_var(node); break;
                    case tpl_ast::FOR_STMT:  visit_for(node); break;
                    case tpl_ast::IF_STMT:   visit_if(node); break;
                    case tpl_ast::SET_STMT:  visit_set(node); break;
                    default: break;
                }
            }
        }
    }

    void visit_var(TinyObjectMap* node) {
        auto* expr_str = node->slot(tpl_ast::EXPRESSION, base_)->as_ptr<ArenaString>(base_);
        out_ += eval_to_string(expr_str->view());
    }

    void visit_for(TinyObjectMap* node) {
        auto* var_str = node->slot(tpl_ast::VARIABLE, base_)->as_ptr<ArenaString>(base_);
        auto* expr_str = node->slot(tpl_ast::EXPRESSION, base_)->as_ptr<ArenaString>(base_);
        auto* body = node->slot(tpl_ast::STATEMENTS, base_)->as_ptr<ObjectArray>(base_);

        std::string var_name(var_str->view());

        // Evaluate the iterable expression.
        // This returns a document whose root should be an array.
        auto iter_result = eval_path_for_iterate(expr_str->view());
        if (!iter_result.has_root()) return;

        auto* arr = HermesCtrAccess::root<ObjectArray>(iter_result);
        if (!arr) return;

        uint8_t* iter_base = HermesCtrAccess::base(iter_result);
        size_t stack_mark = var_stack_.size();
        for (uint64_t i = 0; i < arr->size(); ++i) {
            AnyVal* slot = const_cast<ObjectArray*>(arr)->slot(i, iter_base);
            void* elem = nullptr;
            uint8_t* elem_base = iter_base;
            if (slot->is_pointer()) {
                elem = slot->as_ptr<void>(iter_base);
            } else if (slot->is_value()) {
                // Materialize embedded value into scratch arena.
                elem = materialize_embedded(slot);
                elem_base = HermesCtrAccess::base(scratch_);
            }

            pop_vars_to(stack_mark);
            push_var(var_name, elem, elem_base);
            visit_stmts(body);
        }
        pop_vars_to(stack_mark);
    }

    HermesCtr eval_path_for_iterate(std::string_view expr_text) {
        auto ctx_doc = build_context();
        return eval_path(ctx_doc, expr_text).get();
    }

    void visit_if(TinyObjectMap* node) {
        auto* expr_str = node->slot(tpl_ast::EXPRESSION, base_)->as_ptr<ArenaString>(base_);

        // Evaluate condition.
        void* cond = eval_expr(expr_str->view());
        if (is_truthy(cond)) {
            auto* body = node->slot(tpl_ast::STATEMENTS, base_)->as_ptr<ObjectArray>(base_);
            visit_stmts(body);
        } else if (node->has_key(tpl_ast::ELSE_BRANCH)) {
            auto* else_node = node->slot(tpl_ast::ELSE_BRANCH, base_)->as_ptr<TinyObjectMap>(base_);
            int32_t code = else_node->get(tpl_ast::CODE, base_).as_value<int32_t>();
            if (code == tpl_ast::ELSE_STMT) {
                auto* body = else_node->slot(tpl_ast::STATEMENTS, base_)->as_ptr<ObjectArray>(base_);
                visit_stmts(body);
            } else if (code == tpl_ast::IF_STMT) {
                // elif chain — recursive.
                visit_if(else_node);
            }
        }
    }

    void visit_set(TinyObjectMap* node) {
        auto* var_str = node->slot(tpl_ast::VARIABLE, base_)->as_ptr<ArenaString>(base_);
        auto* expr_str = node->slot(tpl_ast::EXPRESSION, base_)->as_ptr<ArenaString>(base_);
        void* val = eval_expr(expr_str->view());
        push_var(std::string(var_str->view()), val);
    }
};

// ============================================================================
// Public API
// ============================================================================

logos::expected<HermesCtr> parse_template(std::string_view tpl) noexcept {
    try {
        auto doc = make_doc().get();
        TemplateParser parser(tpl, doc);
        void* root = parser.parse();
        HermesCtrAccess::set_root_offset(doc, HermesCtrAccess::offset_of(doc, root));
        return doc;
    } catch (logos::Err& e) {
        return std::unexpected(std::move(e));
    }
}

logos::expected<std::string> render_template(const HermesCtr& tpl,
                                              const HermesCtr& data) noexcept {
    try {
        auto* stmts = HermesCtrAccess::root<ObjectArray>(tpl);
        if (!stmts) return std::string{};
        TemplateRenderer renderer(data);
        return renderer.render(stmts, const_cast<uint8_t*>(HermesCtrAccess::base(tpl)));
    } catch (logos::Err& e) {
        return std::unexpected(std::move(e));
    }
}

logos::expected<std::string> render(std::string_view tpl_text,
                                     const HermesCtr& data) noexcept {
    LOGOS_TRY(auto tpl, parse_template(tpl_text));
    return render_template(tpl, data);
}

} // namespace logos::hermes
