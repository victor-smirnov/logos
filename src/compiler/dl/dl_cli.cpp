// logos-dl: run a rule file the way Souffle's interpreter does, so the two can
// be compared on the same inputs (ADR 0028, tests/dl/oracle.sh).
//
//   logos-dl prog.dl [-F facts_dir] [-D out_dir] [-I include_dir]... [--explain rel]
//
// Each `.input rel` reads `<facts_dir>/rel.facts` (tab-separated); a missing
// file is an error, as in Souffle. Each `.output rel` writes `<out_dir>/rel.csv`
// (tab-separated, rows sorted). `--explain rel` prints the derivation of every
// row of `rel` to stdout.
#include "dl.hpp"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;
using namespace logos::dl;

static std::optional<std::string> read_file(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return std::nullopt;
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

static int usage() {
    std::fprintf(stderr, "usage: logos-dl prog.dl [-F facts_dir] [-D out_dir] [-I dir]... [--explain rel]...\n");
    return 2;
}

int main(int argc, char** argv) {
    std::string prog_path, facts_dir = ".", out_dir = ".";
    std::vector<std::string> inc_dirs, explain;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto need = [&]() -> const char* { return i + 1 < argc ? argv[++i] : nullptr; };
        if (a == "-F") { auto v = need(); if (!v) return usage(); facts_dir = v; }
        else if (a == "-D") { auto v = need(); if (!v) return usage(); out_dir = v; }
        else if (a == "-I") { auto v = need(); if (!v) return usage(); inc_dirs.push_back(v); }
        else if (a == "--explain") { auto v = need(); if (!v) return usage(); explain.push_back(v); }
        else if (!a.empty() && a[0] == '-') return usage();
        else if (prog_path.empty()) prog_path = a;
        else return usage();
    }
    if (prog_path.empty()) return usage();

    auto text = read_file(prog_path);
    if (!text) { std::fprintf(stderr, "logos-dl: cannot read %s\n", prog_path.c_str()); return 1; }
    inc_dirs.insert(inc_dirs.begin(), fs::path(prog_path).parent_path().string());
    IncludeResolver resolver = [&](std::string_view name) -> std::optional<std::string> {
        for (auto& d : inc_dirs)
            if (auto t = read_file(fs::path(d.empty() ? "." : d) / name)) return t;
        return std::nullopt;
    };

    Symbols syms;
    auto prog = Program::parse(*text, prog_path, syms, resolver);
    if (!prog) { std::fprintf(stderr, "logos-dl: %s\n", prog.error().str().c_str()); return 1; }

    Database db(*prog, syms);
    for (uint32_t r = 0; r < prog->relations().size(); ++r) {
        const RelDecl& d = prog->relations()[r];
        if (!d.input) continue;
        fs::path fp = fs::path(facts_dir) / (d.name + ".facts");
        auto ft = read_file(fp);
        if (!ft) { std::fprintf(stderr, "logos-dl: cannot open fact file %s\n", fp.c_str()); return 1; }
        std::istringstream in(*ft);
        std::string line;
        size_t lineno = 0;
        std::vector<Value> row;
        while (std::getline(in, line)) {
            ++lineno;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() && !d.types.empty()) continue;
            row.clear();
            size_t pos = 0;
            for (size_t c = 0; c < d.types.size(); ++c) {
                size_t tab = line.find('\t', pos);
                std::string f = line.substr(pos, tab == std::string::npos ? std::string::npos : tab - pos);
                if (d.types[c] == ColType::Symbol) row.push_back(syms.intern(f));
                else {
                    int32_t v = 0;
                    auto [p, ec] = std::from_chars(f.data(), f.data() + f.size(), v);
                    if (ec != std::errc{} || p != f.data() + f.size()) {
                        std::fprintf(stderr, "logos-dl: %s:%zu: '%s' is not a number\n", fp.c_str(), lineno, f.c_str());
                        return 1;
                    }
                    row.push_back(static_cast<Value>(v));
                }
                if (tab == std::string::npos && c + 1 < d.types.size()) {
                    std::fprintf(stderr, "logos-dl: %s:%zu: expected %zu columns\n", fp.c_str(), lineno, d.types.size());
                    return 1;
                }
                pos = tab == std::string::npos ? line.size() : tab + 1;
            }
            db.insert(r, row);
        }
    }
    db.run();

    std::error_code ec;
    fs::create_directories(out_dir, ec);
    for (uint32_t r = 0; r < prog->relations().size(); ++r) {
        const RelDecl& d = prog->relations()[r];
        if (!d.output) continue;
        const Relation& R = db.relation(r);
        std::vector<std::string> lines;
        for (size_t i = 0; i < R.size(); ++i) {
            std::string s = d.types.empty() ? "()" : "";   // Souffle's nullary row
            auto row = R.row(i);
            for (size_t c = 0; c < d.types.size(); ++c) {
                if (c) s += '\t';
                if (d.types[c] == ColType::Symbol) s += syms.name(row[c]);
                else s += std::to_string(static_cast<int32_t>(row[c]));
            }
            lines.push_back(std::move(s));
        }
        std::sort(lines.begin(), lines.end());
        fs::path op = fs::path(out_dir) / (d.name + ".csv");
        std::ofstream out(op, std::ios::binary);
        if (!out) { std::fprintf(stderr, "logos-dl: cannot write %s\n", op.c_str()); return 1; }
        for (auto& l : lines) out << l << '\n';
    }

    for (auto& name : explain) {
        auto r = prog->relation(name);
        if (!r) { std::fprintf(stderr, "logos-dl: --explain: no relation '%s'\n", name.c_str()); return 1; }
        const Relation& R = db.relation(*r);
        for (size_t i = 0; i < R.size(); ++i)
            if (auto d = db.explain(*r, R.row(i)))
                std::fputs(db.render(*d).c_str(), stdout);
    }
    return 0;
}
