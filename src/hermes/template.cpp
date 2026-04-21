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
#include <logos/core/expected.hpp>

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
    TemplateParser(std::string_view text, Hermes& doc)
        : text_(text), pos_(0), doc_(doc) {}

    logos::expected<void*> parse() noexcept {
        LOGOS_TRY(auto* stmts, HermesAccess::raw_array(doc_));
        LOGOS_TRY_VOID(parse_block(stmts, ""));
        return stmts;
    }

private:
    std::string_view text_;
    size_t pos_;
    Hermes& doc_;
    std::string_view elif_expr_; // Temporary storage for elif expression.

    bool at_end() const noexcept { return pos_ >= text_.size(); }

    // Parse a block of text + statements until we hit an end tag or EOF.
    // end_tag: "endfor", "endif", "else", "elif", or "" for top-level.
    // Returns the keyword that terminated the block.
    logos::expected<std::string> parse_block(ObjectArray* stmts, std::string_view end_tag) noexcept {
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
                LOGOS_TRY(auto* s, HermesAccess::raw_string(doc_, sv));
                LOGOS_TRY_VOID(stmts->push_back(AnyVal{}, HermesAccess::arena(doc_)));
                stmts->slot(stmts->size() - 1, HermesAccess::base(doc_))->set_pointer(s, HermesAccess::base(doc_));
            }

            if (at_end()) break;

            if (text_[pos_ + 1] == '{') {
                // Variable output: {{ expr }}
                LOGOS_TRY_VOID(parse_var_stmt(stmts));
            } else {
                // Statement: {% ... %}
                LOGOS_TRY(std::string keyword, parse_statement(stmts, end_tag));
                if (!keyword.empty()) return keyword;
            }
        }
        return std::string{};
    }

    logos::expected<void> parse_var_stmt(ObjectArray* stmts) noexcept {
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

        // Parse the expression as HermesPath AST (result unused here, just validates).
        LOGOS_TRY_VOID(parse_path(expr_text));

        LOGOS_TRY(auto* expr_str, HermesAccess::raw_string(doc_, expr_text));
        LOGOS_TRY(auto* node, HermesAccess::raw_tiny_map(doc_, 4));
        LOGOS_TRY_VOID(node->put(tpl_ast::CODE, AnyVal::from_value(tpl_ast::VAR_STMT), HermesAccess::arena(doc_)));
        LOGOS_TRY_VOID(node->put(tpl_ast::EXPRESSION, AnyVal{}, HermesAccess::arena(doc_)));
        node->slot(tpl_ast::EXPRESSION, HermesAccess::base(doc_))->set_pointer(expr_str, HermesAccess::base(doc_));

        LOGOS_TRY_VOID(stmts->push_back(AnyVal{}, HermesAccess::arena(doc_)));
        stmts->slot(stmts->size() - 1, HermesAccess::base(doc_))->set_pointer(node, HermesAccess::base(doc_));
        return {};
    }

    // Returns non-empty string if this statement is a block-terminating keyword
    // (endfor, endif, else, elif).
    logos::expected<std::string> parse_statement(ObjectArray* stmts, std::string_view end_tag) noexcept {
        pos_ += 2; // skip {%
        bool strip_before = false;
        if (pos_ < text_.size() && text_[pos_] == '-') { strip_before = true; ++pos_; }
        if (pos_ < text_.size() && text_[pos_] == '+') { ++pos_; } // preserve (default)

        skip_ws();
        logos::expected<std::string> kw_res = read_keyword();
        if (!kw_res) return std::unexpected(std::move(kw_res.error()));
        std::string keyword = std::move(*kw_res);

        if (keyword == "endfor" || keyword == "endif") {
            skip_to_close_tag();
            return keyword;
        }
        if (keyword == "else") {
            skip_to_close_tag();
            return std::string("else");
        }
        if (keyword == "elif") {
            skip_ws();
            std::string_view elif_expr = read_until_close_tag();
            skip_to_close_tag();
            elif_expr_ = elif_expr;
            return std::string("elif");
        }

        if (keyword == "for") {
            LOGOS_TRY_VOID(parse_for_stmt(stmts, strip_before));
            return std::string{};
        }
        if (keyword == "if") {
            LOGOS_TRY_VOID(parse_if_stmt(stmts, strip_before));
            return std::string{};
        }
        if (keyword == "set") {
            LOGOS_TRY_VOID(parse_set_stmt(stmts));
            return std::string{};
        }

        // Unknown statement — skip.
        skip_to_close_tag();
        return std::string{};
    }

    logos::expected<void> parse_for_stmt(ObjectArray* stmts, bool /*strip*/) noexcept {
        skip_ws();
        logos::expected<std::string> var_res = read_keyword();
        if (!var_res) return std::unexpected(std::move(var_res.error()));
        skip_ws();
        logos::expected<std::string> in_res = read_keyword();
        if (!in_res) return std::unexpected(std::move(in_res.error()));
        std::string var_name = std::move(*var_res);
        std::string in_kw = std::move(*in_res);
        if (in_kw != "in") return std::unexpected(logos::err(ErrCode::template_error));
        skip_ws();
        std::string_view expr_text = read_until_close_tag();
        skip_to_close_tag();

        // Parse body.
        LOGOS_TRY(auto* body, HermesAccess::raw_array(doc_));
        LOGOS_TRY(std::string end_kw, parse_block(body, "endfor"));
        if (end_kw != "endfor") return std::unexpected(logos::err(ErrCode::template_error));

        // Allocate strings first, then build the node.
        LOGOS_TRY(auto* var_str, HermesAccess::raw_string(doc_, var_name));
        LOGOS_TRY(auto* expr_str, HermesAccess::raw_string(doc_, expr_text));

        LOGOS_TRY(auto* node, HermesAccess::raw_tiny_map(doc_, 8));
        LOGOS_TRY_VOID(node->put(tpl_ast::CODE, AnyVal::from_value(tpl_ast::FOR_STMT), HermesAccess::arena(doc_)));
        LOGOS_TRY_VOID(node->put(tpl_ast::VARIABLE, AnyVal{}, HermesAccess::arena(doc_)));
        LOGOS_TRY_VOID(node->put(tpl_ast::EXPRESSION, AnyVal{}, HermesAccess::arena(doc_)));
        LOGOS_TRY_VOID(node->put(tpl_ast::STATEMENTS, AnyVal{}, HermesAccess::arena(doc_)));
        // Now set pointers — all allocations are done, no more arena mutations.
        node->slot(tpl_ast::VARIABLE, HermesAccess::base(doc_))->set_pointer(var_str, HermesAccess::base(doc_));
        node->slot(tpl_ast::EXPRESSION, HermesAccess::base(doc_))->set_pointer(expr_str, HermesAccess::base(doc_));
        node->slot(tpl_ast::STATEMENTS, HermesAccess::base(doc_))->set_pointer(body, HermesAccess::base(doc_));

        LOGOS_TRY_VOID(stmts->push_back(AnyVal{}, HermesAccess::arena(doc_)));
        stmts->slot(stmts->size() - 1, HermesAccess::base(doc_))->set_pointer(node, HermesAccess::base(doc_));
        return {};
    }

    logos::expected<void> parse_if_stmt(ObjectArray* stmts, bool /*strip*/) noexcept {
        skip_ws();
        std::string_view expr_text = read_until_close_tag();
        skip_to_close_tag();

        LOGOS_TRY(auto* body, HermesAccess::raw_array(doc_));
        LOGOS_TRY(std::string end_kw, parse_block(body, "endif"));

        LOGOS_TRY(auto* expr_str, HermesAccess::raw_string(doc_, expr_text));
        LOGOS_TRY(auto* node, HermesAccess::raw_tiny_map(doc_, 8));
        LOGOS_TRY_VOID(node->put(tpl_ast::CODE, AnyVal::from_value(tpl_ast::IF_STMT), HermesAccess::arena(doc_)));
        LOGOS_TRY_VOID(node->put(tpl_ast::EXPRESSION, AnyVal{}, HermesAccess::arena(doc_)));
        LOGOS_TRY_VOID(node->put(tpl_ast::STATEMENTS, AnyVal{}, HermesAccess::arena(doc_)));
        node->slot(tpl_ast::EXPRESSION, HermesAccess::base(doc_))->set_pointer(expr_str, HermesAccess::base(doc_));
        node->slot(tpl_ast::STATEMENTS, HermesAccess::base(doc_))->set_pointer(body, HermesAccess::base(doc_));

        // Handle else/elif chain.
        if (end_kw == "else") {
            LOGOS_TRY(auto* else_body, HermesAccess::raw_array(doc_));
            LOGOS_TRY_VOID(parse_block(else_body, "endif"));
            LOGOS_TRY(auto* else_node, HermesAccess::raw_tiny_map(doc_, 4));
            LOGOS_TRY_VOID(else_node->put(tpl_ast::CODE, AnyVal::from_value(tpl_ast::ELSE_STMT), HermesAccess::arena(doc_)));
            LOGOS_TRY_VOID(else_node->put(tpl_ast::STATEMENTS, AnyVal{}, HermesAccess::arena(doc_)));
            else_node->slot(tpl_ast::STATEMENTS, HermesAccess::base(doc_))->set_pointer(else_body, HermesAccess::base(doc_));
            LOGOS_TRY_VOID(node->put(tpl_ast::ELSE_BRANCH, AnyVal{}, HermesAccess::arena(doc_)));
            node->slot(tpl_ast::ELSE_BRANCH, HermesAccess::base(doc_))->set_pointer(else_node, HermesAccess::base(doc_));
        } else if (end_kw == "elif") {
            // Recursive: build a nested if from the elif.
            LOGOS_TRY(auto* elif_stmts, HermesAccess::raw_array(doc_));
            LOGOS_TRY_VOID(parse_if_stmt_with_expr(elif_stmts, elif_expr_));
            if (elif_stmts->size() > 0) {
                LOGOS_TRY_VOID(node->put(tpl_ast::ELSE_BRANCH, AnyVal{}, HermesAccess::arena(doc_)));
                node->slot(tpl_ast::ELSE_BRANCH, HermesAccess::base(doc_))->set_pointer(
                    elif_stmts->slot(0, HermesAccess::base(doc_))->as_ptr<void>(HermesAccess::base(doc_)), HermesAccess::base(doc_));
            }
        }

        LOGOS_TRY_VOID(stmts->push_back(AnyVal{}, HermesAccess::arena(doc_)));
        stmts->slot(stmts->size() - 1, HermesAccess::base(doc_))->set_pointer(node, HermesAccess::base(doc_));
        return {};
    }

    logos::expected<void> parse_if_stmt_with_expr(ObjectArray* stmts, std::string_view expr_text) noexcept {
        LOGOS_TRY(auto* body, HermesAccess::raw_array(doc_));
        LOGOS_TRY(std::string end_kw, parse_block(body, "endif"));

        LOGOS_TRY(auto* expr_str, HermesAccess::raw_string(doc_, expr_text));
        LOGOS_TRY(auto* node, HermesAccess::raw_tiny_map(doc_, 8));
        LOGOS_TRY_VOID(node->put(tpl_ast::CODE, AnyVal::from_value(tpl_ast::IF_STMT), HermesAccess::arena(doc_)));
        LOGOS_TRY_VOID(node->put(tpl_ast::EXPRESSION, AnyVal{}, HermesAccess::arena(doc_)));
        LOGOS_TRY_VOID(node->put(tpl_ast::STATEMENTS, AnyVal{}, HermesAccess::arena(doc_)));
        node->slot(tpl_ast::EXPRESSION, HermesAccess::base(doc_))->set_pointer(expr_str, HermesAccess::base(doc_));
        node->slot(tpl_ast::STATEMENTS, HermesAccess::base(doc_))->set_pointer(body, HermesAccess::base(doc_));

        if (end_kw == "else") {
            LOGOS_TRY(auto* else_body, HermesAccess::raw_array(doc_));
            LOGOS_TRY_VOID(parse_block(else_body, "endif"));
            LOGOS_TRY(auto* else_node, HermesAccess::raw_tiny_map(doc_, 4));
            LOGOS_TRY_VOID(else_node->put(tpl_ast::CODE, AnyVal::from_value(tpl_ast::ELSE_STMT), HermesAccess::arena(doc_)));
            LOGOS_TRY_VOID(else_node->put(tpl_ast::STATEMENTS, AnyVal{}, HermesAccess::arena(doc_)));
            else_node->slot(tpl_ast::STATEMENTS, HermesAccess::base(doc_))->set_pointer(else_body, HermesAccess::base(doc_));
            LOGOS_TRY_VOID(node->put(tpl_ast::ELSE_BRANCH, AnyVal{}, HermesAccess::arena(doc_)));
            node->slot(tpl_ast::ELSE_BRANCH, HermesAccess::base(doc_))->set_pointer(else_node, HermesAccess::base(doc_));
        } else if (end_kw == "elif") {
            LOGOS_TRY(auto* elif_stmts, HermesAccess::raw_array(doc_));
            LOGOS_TRY_VOID(parse_if_stmt_with_expr(elif_stmts, elif_expr_));
            if (elif_stmts->size() > 0) {
                LOGOS_TRY_VOID(node->put(tpl_ast::ELSE_BRANCH, AnyVal{}, HermesAccess::arena(doc_)));
                node->slot(tpl_ast::ELSE_BRANCH, HermesAccess::base(doc_))->set_pointer(
                    elif_stmts->slot(0, HermesAccess::base(doc_))->as_ptr<void>(HermesAccess::base(doc_)), HermesAccess::base(doc_));
            }
        }

        LOGOS_TRY_VOID(stmts->push_back(AnyVal{}, HermesAccess::arena(doc_)));
        stmts->slot(stmts->size() - 1, HermesAccess::base(doc_))->set_pointer(node, HermesAccess::base(doc_));
        return {};
    }

    logos::expected<void> parse_set_stmt(ObjectArray* stmts) noexcept {
        skip_ws();
        logos::expected<std::string> var_res = read_keyword();
        if (!var_res) return std::unexpected(std::move(var_res.error()));
        std::string var_name = std::move(*var_res);

        skip_ws();
        if (pos_ < text_.size() && text_[pos_] == '=') ++pos_;
        skip_ws();
        std::string_view expr_text = read_until_close_tag();
        skip_to_close_tag();

        LOGOS_TRY(auto* var_str, HermesAccess::raw_string(doc_, var_name));
        LOGOS_TRY(auto* expr_str, HermesAccess::raw_string(doc_, expr_text));
        LOGOS_TRY(auto* node, HermesAccess::raw_tiny_map(doc_, 4));
        LOGOS_TRY_VOID(node->put(tpl_ast::CODE, AnyVal::from_value(tpl_ast::SET_STMT), HermesAccess::arena(doc_)));
        LOGOS_TRY_VOID(node->put(tpl_ast::VARIABLE, AnyVal{}, HermesAccess::arena(doc_)));
        LOGOS_TRY_VOID(node->put(tpl_ast::EXPRESSION, AnyVal{}, HermesAccess::arena(doc_)));
        node->slot(tpl_ast::VARIABLE, HermesAccess::base(doc_))->set_pointer(var_str, HermesAccess::base(doc_));
        node->slot(tpl_ast::EXPRESSION, HermesAccess::base(doc_))->set_pointer(expr_str, HermesAccess::base(doc_));

        LOGOS_TRY_VOID(stmts->push_back(AnyVal{}, HermesAccess::arena(doc_)));
        stmts->slot(stmts->size() - 1, HermesAccess::base(doc_))->set_pointer(node, HermesAccess::base(doc_));
        return {};
    }

    // --- Lexer helpers ---

    void skip_ws() noexcept {
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

    std::string_view read_until_close_tag() noexcept {
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

    void skip_to_close_tag() noexcept {
        while (pos_ + 1 < text_.size()) {
            if (text_[pos_] == '%' && text_[pos_ + 1] == '}') { pos_ += 2; return; }
            if (text_[pos_] == '-' && text_[pos_ + 1] == '}') { pos_ += 2; return; }
            if (text_[pos_] == '+' && text_[pos_ + 1] == '}') { pos_ += 2; return; }
            ++pos_;
        }
    }
};

// ============================================================================
// Template Renderer
// ============================================================================

class TemplateRenderer {
public:
    TemplateRenderer(const Hermes& data)
        : data_(data) {}

    logos::expected<std::string> render(const ObjectArray* stmts, uint8_t* tpl_base) noexcept {
        base_ = tpl_base;  // Template AST base for reading nodes.
        LOGOS_TRY_VOID(visit_stmts(stmts));
        return std::move(out_);
    }

private:
    const Hermes& data_;
    std::string out_;
    uint8_t* base_ = nullptr;  // Template AST arena base.
    Hermes scratch_ = make_doc_multi(4096).value_or(Hermes{}); // Scratch arena; OOM → terminate via get() later.

    // Variable scope stack.
    struct VarBinding {
        std::string name;
        void* value;
        uint8_t* base;  // arena base of value
    };
    std::vector<VarBinding> var_stack_;

    logos::expected<void> push_var(const std::string& name, void* val, uint8_t* val_base = nullptr) noexcept {
        var_stack_.push_back({name, val, val_base});
        return {};
    }

    void pop_vars_to(size_t mark) noexcept {
        var_stack_.resize(mark);
    }

    void* find_var(std::string_view name) noexcept {
        // Search stack in reverse (most recent first).
        for (auto it = var_stack_.rbegin(); it != var_stack_.rend(); ++it) {
            if (it->name == name) return it->value;
        }
        return nullptr;
    }

    // Evaluate a HermesPath expression against current context.
    // Build a fresh context doc with data + template variables.
    logos::expected<Hermes> build_context() noexcept {
        LOGOS_TRY(auto ctx_doc, make_doc_multi());
        LOGOS_TRY(auto* ctx, HermesAccess::raw_object_map(ctx_doc));
        HermesAccess::set_root(ctx_doc, ctx);

        // Copy data fields.
        void* data_root = HermesAccess::root<void>(data_);
        if (data_root) {
            auto* db = static_cast<const uint8_t*>(data_root);
            TypeTag tag = TypeTag::read_before(db);
            if (tag.descriptor() == TagDescriptor::Map && tag.type_code() == type_hash::ObjectMap) {
                auto* data_map = static_cast<ObjectMap*>(data_root);
                uint8_t* data_base = const_cast<uint8_t*>(HermesAccess::base(data_));
                logos::expected<void> status{};
                data_map->for_each([&](ArenaString* key, AnyVal* val) noexcept {
                    if (!status) return;
                    if (val->is_value()) {
                        auto res = ctx->put(key->view(), *val, HermesAccess::arena(ctx_doc));
                        if (!res) { status = std::unexpected(std::move(res.error())); return; }
                    } else if (val->is_pointer()) {
                        auto res = put_into_ctx(ctx, ctx_doc, key->view(), val->as_ptr<void>(data_base), data_base);
                        if (!res) { status = std::unexpected(std::move(res.error())); return; }
                    }
                }, data_base);
                LOGOS_TRY_VOID(std::move(status));
            }
        }

        // Overlay template variables.
        for (auto& binding : var_stack_) {
            if (!binding.value) continue;
            LOGOS_TRY_VOID(put_into_ctx(ctx, ctx_doc, binding.name, binding.value, binding.base));
        }

        return ctx_doc;
    }

    logos::expected<void*> eval_expr(std::string_view expr_text) noexcept {
        LOGOS_TRY(auto ctx_doc, build_context());
        LOGOS_TRY(auto result, eval_path(ctx_doc, expr_text));
        if (!result.has_root()) return static_cast<void*>(nullptr);
        return HermesAccess::root<void>(result);
    }

    logos::expected<std::string> eval_to_string(std::string_view expr_text) noexcept {
        LOGOS_TRY(auto ctx_doc, build_context());
        LOGOS_TRY(auto res, eval_path(ctx_doc, expr_text));
        if (!res.has_root()) {
            return std::string{};
        }
        return value_to_string(HermesAccess::root<void>(res));
    }

    logos::expected<std::string> value_to_string(void* val) noexcept {
        if (!val) {
            return std::string{};
        }
        auto* b = static_cast<const uint8_t*>(val);
        TypeTag tag = TypeTag::read_before(b);
        switch (tag.type_code()) {
            case type_hash::HermesString: return std::string(static_cast<const ArenaString*>(val)->view());
            case type_hash::I24: return std::to_string(*static_cast<const int32_t*>(val));
            case type_hash::I64:  return std::to_string(*static_cast<const int64_t*>(val));
            case type_hash::F32: {
                char buf[32];
                int n = std::snprintf(buf, sizeof(buf), "%g", *static_cast<const float*>(val));
                return std::string(buf, n);
            }
            case type_hash::F64: {
                char buf[32];
                int n = std::snprintf(buf, sizeof(buf), "%g", *static_cast<const double*>(val));
                return std::string(buf, n);
            }
            case type_hash::Bool:
                return *static_cast<const uint8_t*>(val) ? std::string("true") : std::string("false");
            default: return std::string{};
        }
    }

    // Put an arena object into context map, copying if needed to avoid cross-arena pointers.
    // NOTE: ctx may be invalidated after this call (arena growth). Re-derive via offset.
    logos::expected<void> put_into_ctx(ObjectMap* ctx, Hermes& ctx_doc, std::string_view key, void* val,
                     uint8_t* val_base = nullptr) noexcept {
        if (!val) {
            LOGOS_TRY_VOID(ctx->put(key, AnyVal{}, HermesAccess::arena(ctx_doc)));
            return {};
        }
        auto* b = static_cast<const uint8_t*>(val);
        TypeTag tag = TypeTag::read_before(b);
        uint64_t tc = tag.type_code();
        // Embeddable scalars: store as embedded AnyVal (no pointer needed).
        if (tc == type_hash::I24) {
            LOGOS_TRY_VOID(ctx->put(key, AnyVal::from_value(*static_cast<const int32_t*>(val), tc), HermesAccess::arena(ctx_doc)));
            return {};
        } else if (tc == type_hash::U24) {
            LOGOS_TRY_VOID(ctx->put(key, AnyVal::from_value(*static_cast<const uint32_t*>(val), tc), HermesAccess::arena(ctx_doc)));
            return {};
        } else if (tc == type_hash::Bool) {
            LOGOS_TRY_VOID(ctx->put(key, AnyVal::from_value(*static_cast<const uint8_t*>(val), tc), HermesAccess::arena(ctx_doc)));
            return {};
        } else if (tc == type_hash::F32) {
            LOGOS_TRY(AnyVal av,
                anyval_put<float>(HermesAccess::arena(ctx_doc),
                    *static_cast<const float*>(val)));
            LOGOS_TRY_VOID(ctx->put(key, av, HermesAccess::arena(ctx_doc)));
            return {};
        } else if (tc == type_hash::I16) {
            LOGOS_TRY_VOID(ctx->put(key, AnyVal::from_value(*static_cast<const int16_t*>(val), tc), HermesAccess::arena(ctx_doc)));
            return {};
        } else if (tc == type_hash::I8) {
            LOGOS_TRY_VOID(ctx->put(key, AnyVal::from_value(*static_cast<const int8_t*>(val), tc), HermesAccess::arena(ctx_doc)));
            return {};
        } else if (tc == type_hash::HermesString) {
            auto* s = static_cast<const ArenaString*>(val);
            LOGOS_TRY(auto* copy, ArenaString::create(HermesAccess::arena(ctx_doc), s->view()));
            uint8_t* cb = HermesAccess::base(ctx_doc);
            LOGOS_TRY_VOID(ctx->put(key, AnyVal{}, HermesAccess::arena(ctx_doc)));
            ctx->get_slot(key, cb)->set_pointer(copy, cb);
            return {};
        } else {
            // Complex types (arrays, maps): deep-copy into ctx_doc so offsets are consistent.
            uint8_t* vb = val_base ? val_base : const_cast<uint8_t*>(HermesAccess::base(data_));
            LOGOS_TRY(void* copy, copy_object_into(val, vb, ctx_doc));
            uint8_t* cb = HermesAccess::base(ctx_doc);
            LOGOS_TRY_VOID(ctx->put(key, AnyVal{}, HermesAccess::arena(ctx_doc)));
            ctx->get_slot(key, cb)->set_pointer(copy, cb);
            return {};
        }
    }

    // Materialize an embedded AnyVal value into an arena-allocated object.
    // Uses a scratch doc so the pointer stays valid for the template's lifetime.
    logos::expected<void*> materialize_embedded(AnyVal* slot) noexcept {
        if (!slot || slot->is_null()) return static_cast<void*>(nullptr);
        uint8_t th = slot->value_type_hash();
        switch (th) {
            case type_hash::I24: {
                return HermesAccess::make_value<int32_t>(scratch_, slot->as_value<int32_t>());
            }
            case type_hash::U24: {
                return HermesAccess::make_value<uint32_t>(scratch_, slot->as_value<uint32_t>());
            }
            case type_hash::Bool: {
                TypeTag btag(type_hash::Bool, TagDescriptor::Data);
                LOGOS_TRY(void* mem, HermesAccess::arena(scratch_).allocate(1, 2, btag));
                *static_cast<uint8_t*>(mem) = slot->as_value<uint8_t>();
                return mem;
            }
            // Real (float) is never embedded under the 4-byte AnyVal layout,
            // so it cannot appear here — materialize_embedded only sees
            // value-mode slots.
            case type_hash::I16: {
                return HermesAccess::make_value<int16_t>(scratch_, slot->as_value<int16_t>());
            }
            case type_hash::I8: {
                return HermesAccess::make_value<int8_t>(scratch_, slot->as_value<int8_t>());
            }
            default: return static_cast<void*>(nullptr);
        }
    }

    bool is_truthy(void* val) noexcept {
        if (!val) return false;
        auto* b = static_cast<const uint8_t*>(val);
        TypeTag tag = TypeTag::read_before(b);
        uint64_t tc = tag.type_code();
        if (tc == type_hash::Bool) return *static_cast<const uint8_t*>(val) != 0;
        if (tc == type_hash::I24) return *static_cast<const int32_t*>(val) != 0;
        if (tc == type_hash::HermesString) return static_cast<const ArenaString*>(val)->length() > 0;
        if (tag.descriptor() == TagDescriptor::Array) return static_cast<const ObjectArray*>(val)->size() > 0;
        return true;
    }

    // --- Visit methods ---

    logos::expected<void> visit_stmts(const ObjectArray* stmts) noexcept {
        auto* stmts_mut = const_cast<ObjectArray*>(stmts);
        for (uint64_t i = 0; i < stmts->size(); ++i) {
            AnyVal* slot = stmts_mut->slot(i, base_);
            if (slot->is_null()) continue;
            if (!slot->is_pointer()) continue;
            void* elem = slot->as_ptr<void>(base_);
            auto* eb = static_cast<const uint8_t*>(elem);
            TypeTag tag = TypeTag::read_before(eb);

            if (tag.type_code() == type_hash::HermesString) {
                // Text node.
                out_ += static_cast<const ArenaString*>(elem)->view();
            } else if (tag.descriptor() == TagDescriptor::Map && tag.type_code() == type_hash::TinyObjectMap) {
                // Statement node (TinyObjectMap).
                auto* node = static_cast<TinyObjectMap*>(elem);
                int32_t code = node->get(tpl_ast::CODE, base_).as_value<int32_t>();
                switch (code) {
                    case tpl_ast::VAR_STMT:  { LOGOS_TRY_VOID(visit_var(node)); break; }
                    case tpl_ast::FOR_STMT:  { LOGOS_TRY_VOID(visit_for(node)); break; }
                    case tpl_ast::IF_STMT:   { LOGOS_TRY_VOID(visit_if(node)); break; }
                    case tpl_ast::SET_STMT:  { LOGOS_TRY_VOID(visit_set(node)); break; }
                    default: break;
                }
            }
        }
        return {};
    }

    logos::expected<void> visit_var(TinyObjectMap* node) noexcept {
        auto* expr_str = node->slot(tpl_ast::EXPRESSION, base_)->as_ptr<ArenaString>(base_);
        LOGOS_TRY(auto s, eval_to_string(expr_str->view()));
        out_ += s;
        return {};
    }

    logos::expected<void> visit_for(TinyObjectMap* node) noexcept {
        auto* var_str = node->slot(tpl_ast::VARIABLE, base_)->as_ptr<ArenaString>(base_);
        auto* expr_str = node->slot(tpl_ast::EXPRESSION, base_)->as_ptr<ArenaString>(base_);
        auto* body = node->slot(tpl_ast::STATEMENTS, base_)->as_ptr<ObjectArray>(base_);

        std::string var_name(var_str->view());

        // Evaluate the iterable expression.
        LOGOS_TRY(auto iter_result, eval_path_for_iterate(expr_str->view()));
        if (!iter_result.has_root()) return {};

        auto* arr = HermesAccess::root<ObjectArray>(iter_result);
        if (!arr) return {};

        uint8_t* iter_base = HermesAccess::base(iter_result);
        size_t stack_mark = var_stack_.size();
        for (uint64_t i = 0; i < arr->size(); ++i) {
            AnyVal* slot = const_cast<ObjectArray*>(arr)->slot(i, iter_base);
            void* elem = nullptr;
            uint8_t* elem_base = iter_base;
            if (slot->is_pointer()) {
                elem = slot->as_ptr<void>(iter_base);
            } else if (slot->is_value()) {
                // Materialize embedded value into scratch arena.
                LOGOS_TRY(elem, materialize_embedded(slot));
                elem_base = HermesAccess::base(scratch_);
            }

            pop_vars_to(stack_mark);
            LOGOS_TRY_VOID(push_var(var_name, elem, elem_base));
            LOGOS_TRY_VOID(visit_stmts(body));
        }
        pop_vars_to(stack_mark);
        return {};
    }

    logos::expected<Hermes> eval_path_for_iterate(std::string_view expr_text) noexcept {
        LOGOS_TRY(auto ctx_doc, build_context());
        return eval_path(ctx_doc, expr_text);
    }

    logos::expected<void> visit_if(TinyObjectMap* node) noexcept {
        auto* expr_str = node->slot(tpl_ast::EXPRESSION, base_)->as_ptr<ArenaString>(base_);

        // Evaluate condition.
        LOGOS_TRY(void* cond, eval_expr(expr_str->view()));
        if (is_truthy(cond)) {
            auto* body = node->slot(tpl_ast::STATEMENTS, base_)->as_ptr<ObjectArray>(base_);
            LOGOS_TRY_VOID(visit_stmts(body));
        } else if (node->has_key(tpl_ast::ELSE_BRANCH)) {
            auto* else_node = node->slot(tpl_ast::ELSE_BRANCH, base_)->as_ptr<TinyObjectMap>(base_);
            int32_t code = else_node->get(tpl_ast::CODE, base_).as_value<int32_t>();
            if (code == tpl_ast::ELSE_STMT) {
                auto* body = else_node->slot(tpl_ast::STATEMENTS, base_)->as_ptr<ObjectArray>(base_);
                LOGOS_TRY_VOID(visit_stmts(body));
            } else if (code == tpl_ast::IF_STMT) {
                // elif chain — recursive.
                LOGOS_TRY_VOID(visit_if(else_node));
            }
        }
        return {};
    }

    logos::expected<void> visit_set(TinyObjectMap* node) noexcept {
        auto* var_str = node->slot(tpl_ast::VARIABLE, base_)->as_ptr<ArenaString>(base_);
        auto* expr_str = node->slot(tpl_ast::EXPRESSION, base_)->as_ptr<ArenaString>(base_);
        LOGOS_TRY(void* val, eval_expr(expr_str->view()));
        LOGOS_TRY_VOID(push_var(std::string(var_str->view()), val));
        return {};
    }
};

// ============================================================================
// Public API
// ============================================================================

logos::expected<Hermes> parse_template(std::string_view tpl) noexcept {
    LOGOS_TRY(auto doc, make_doc());
    TemplateParser parser(tpl, doc);
    LOGOS_TRY(void* root, parser.parse());
    HermesAccess::set_root_offset(doc, HermesAccess::offset_of(doc, root));
    return doc;
}

logos::expected<std::string> render_template(const Hermes& tpl,
                                              const Hermes& data) noexcept {
    auto* stmts = HermesAccess::root<ObjectArray>(tpl);
    if (!stmts) return std::string{};
    TemplateRenderer renderer(data);
    return renderer.render(stmts, const_cast<uint8_t*>(HermesAccess::base(tpl)));
}

logos::expected<std::string> render(std::string_view tpl_text,
                                     const Hermes& data) noexcept {
    LOGOS_TRY(auto tpl, parse_template(tpl_text));
    return render_template(tpl, data);
}

} // namespace logos::hermes
