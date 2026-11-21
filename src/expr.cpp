// Meridian: the expression compiler and evaluator. See expr.h for the language.
//
// Layout of this file: the list of functions, then the Compiler (a small recursive-descent parser that writes the steps
// straight into a vector), then Program::eval (a loop over those steps).
#include "expr.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace expr {

namespace {

// ---- built-in functions. To add one: add an id here, a row in FNS (same order!), and a case in Program::eval.
enum Fn { FN_SIN, FN_COS, FN_TAN, FN_ATAN2, FN_ABS, FN_SQRT, FN_POW, FN_EXP, FN_LOG, FN_FLOOR, FN_CEIL, FN_ROUND, FN_FRACT, FN_SIGN,
          FN_MIN, FN_MAX, FN_CLAMP, FN_MIX, FN_SMOOTH, FN_STEP, FN_HASH, FN_MOD, FN_COUNT };
struct FnInfo { const char* name; int argc; };
const FnInfo FNS[FN_COUNT] = {
    {"sin", 1}, {"cos", 1}, {"tan", 1}, {"atan2", 2}, {"abs", 1}, {"sqrt", 1}, {"pow", 2}, {"exp", 1}, {"log", 1}, {"floor", 1}, {"ceil", 1},
    {"round", 1}, {"fract", 1}, {"sign", 1}, {"min", 2}, {"max", 2}, {"clamp", 3}, {"mix", 3}, {"smoothstep", 3}, {"step", 2}, {"hash", 1}, {"mod", 2},
};

const int MAX_STACK = 48;       // deepest stack a formula may need (checked when compiling, so eval never overflows)
// Limits that keep a broken or hostile formula from hurting the clock. The parser calls itself for every "(", every prefix sign and
// every ?: (so a file with thousands of "(" in a row would overflow the program's own stack), so the nesting is counted while parsing.
const int MAX_NEST = 64;        // levels of "(" inside "(" ... (a bracket costs two levels), "- - -" prefixes and a ? b : c ? d : e chains
const size_t MAX_LEN = 2000;    // characters in one formula
const size_t MAX_INS = 1500;    // steps in one compiled formula

// The same kind of integer hash the built-in scenes use: the same n always gives the same number from 0 to 1.
double hash01(double n) {
    if (!std::isfinite(n)) return 0.0;                              // a cast of NaN or a huge number to an integer is undefined
    n = std::max(-1e15, std::min(1e15, n));
    unsigned x = (unsigned)(long long)std::floor(n) * 374761393u + 668265263u;
    x = (x ^ (x >> 13)) * 1274126177u; x ^= x >> 16;
    return (x & 0xFFFF) / 65535.0;
}

} // namespace

// ---------------------------------------------------------------- compiler
struct Compiler {
    const char* p;
    const VarTable& vars;
    std::vector<Program::Ins>& out;
    std::string err;
    int depth = 0, maxDepth = 0;
    int nest = 0;

    Compiler(const std::string& s, const VarTable& v, std::vector<Program::Ins>& o) : p(s.c_str()), vars(v), out(o) {}

    // counts one level of nesting for as long as it lives; asks to stop when there are too many (or too many steps were written)
    struct Level {
        Compiler& c; bool ok;
        explicit Level(Compiler& cc) : c(cc), ok(true) {
            if (++c.nest > MAX_NEST) { ok = false; c.fail("the formula has too many brackets or signs nested inside each other"); }
            else if (c.out.size() > MAX_INS) { ok = false; c.fail("the formula is too long"); }
        }
        ~Level() { --c.nest; }
    };

    void ws() { while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') ++p; }
    bool fail(const std::string& m) { if (err.empty()) err = m; return false; }
    void adjust(int d) { depth += d; if (depth > maxDepth) maxDepth = depth; }
    void emit(Program::Op op, int a = 0, double val = 0) { Program::Ins i; i.op = op; i.a = a; i.val = val; out.push_back(i); }
    bool accept(const char* s) { ws(); size_t n = strlen(s); if (strncmp(p, s, n) == 0) { p += n; return true; } return false; }

    bool ternary() {
        Level lv(*this); if (!lv.ok) return false;
        if (!orExpr()) return false;
        ws();
        if (*p == '?') {
            ++p;
            if (!ternary()) return false;
            ws(); if (*p != ':') return fail("expected ':' in a ? b : c");
            ++p;
            if (!ternary()) return false;
            emit(Program::SEL); adjust(-2);
        }
        return true;
    }
    bool orExpr() {
        if (!andExpr()) return false;
        while (accept("||")) { if (!andExpr()) return false; emit(Program::OR); adjust(-1); }
        return true;
    }
    bool andExpr() {
        if (!eqExpr()) return false;
        while (accept("&&")) { if (!eqExpr()) return false; emit(Program::AND); adjust(-1); }
        return true;
    }
    bool eqExpr() {
        if (!cmpExpr()) return false;
        for (;;) {
            if (accept("==")) { if (!cmpExpr()) return false; emit(Program::EQ); adjust(-1); }
            else if (accept("!=")) { if (!cmpExpr()) return false; emit(Program::NE); adjust(-1); }
            else return true;
        }
    }
    bool cmpExpr() {
        if (!addExpr()) return false;
        for (;;) {
            if (accept("<=")) { if (!addExpr()) return false; emit(Program::LE); adjust(-1); }
            else if (accept(">=")) { if (!addExpr()) return false; emit(Program::GE); adjust(-1); }
            else if (accept("<")) { if (!addExpr()) return false; emit(Program::LT); adjust(-1); }
            else if (accept(">")) { if (!addExpr()) return false; emit(Program::GT); adjust(-1); }
            else return true;
        }
    }
    bool addExpr() {
        if (!mulExpr()) return false;
        for (;;) {
            ws();
            if (*p == '+') { ++p; if (!mulExpr()) return false; emit(Program::ADD); adjust(-1); }
            else if (*p == '-') { ++p; if (!mulExpr()) return false; emit(Program::SUB); adjust(-1); }
            else return true;
        }
    }
    bool mulExpr() {
        if (!unary()) return false;
        for (;;) {
            ws();
            if (*p == '*') { ++p; if (!unary()) return false; emit(Program::MUL); adjust(-1); }
            else if (*p == '/') { ++p; if (!unary()) return false; emit(Program::DIV); adjust(-1); }
            else if (*p == '%') { ++p; if (!unary()) return false; emit(Program::MOD); adjust(-1); }
            else return true;
        }
    }
    bool unary() {
        Level lv(*this); if (!lv.ok) return false;
        ws();
        if (*p == '-') {
            ++p;
            const size_t before = out.size();
            if (!unary()) return false;
            // constant-fold negative literals so isConst() sees "-0.5" as a single PUSH
            if (out.size() == before + 1 && out.back().op == Program::PUSH) out.back().val = -out.back().val;
            else emit(Program::NEG);
            return true;
        }
        if (*p == '+') { ++p; return unary(); }
        if (*p == '!' && p[1] != '=') { ++p; if (!unary()) return false; emit(Program::NOT); return true; }
        return primary();
    }
    bool primary() {
        ws();
        if (*p == '(') {
            ++p; if (!ternary()) return false;
            ws(); if (*p != ')') return fail("missing ')'");
            ++p; return true;
        }
        if ((*p >= '0' && *p <= '9') || *p == '.') {
            char* e = nullptr; double v = strtod(p, &e);
            if (e == p) return fail("bad number");
            if (!std::isfinite(v)) return fail("number too large");
            p = e; emit(Program::PUSH, 0, v); adjust(1); return true;
        }
        if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || *p == '_') {
            const char* s = p;
            while ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '_') ++p;
            std::string name(s, p - s);
            ws();
            if (*p == '(') {                                           // function call
                int id = -1;
                for (int i = 0; i < FN_COUNT; ++i) if (name == FNS[i].name) id = i;
                if (id < 0) return fail("unknown function '" + name + "'");
                ++p; int argc = 0;
                ws();
                if (*p != ')') {
                    for (;;) {
                        if (!ternary()) return false;
                        ++argc; ws();
                        if (*p == ',') { ++p; continue; }
                        break;
                    }
                }
                ws(); if (*p != ')') return fail("missing ')' after the arguments of " + name);
                ++p;
                if (argc != FNS[id].argc) return fail(name + "() takes " + std::to_string(FNS[id].argc) + " value(s)");
                emit(Program::CALL, id); adjust(-(argc - 1));
                return true;
            }
            if (name == "pi") { emit(Program::PUSH, 0, 3.14159265358979323846); adjust(1); return true; }
            if (name == "true") { emit(Program::PUSH, 0, 1.0); adjust(1); return true; }
            if (name == "false") { emit(Program::PUSH, 0, 0.0); adjust(1); return true; }
            const int slot = vars.find(name);
            if (slot < 0) return fail("unknown name '" + name + "'");
            emit(Program::LOAD, slot); adjust(1); return true;
        }
        return fail(*p ? std::string("unexpected '") + *p + "'" : std::string("the formula ends too early"));
    }
};

bool Program::compile(const std::string& src, const VarTable& vars, std::string& err) {
    code_.clear();
    if (src.size() > MAX_LEN) { err = "the formula is too long (over 2000 characters)"; return false; }
    if (src.find('\0') != std::string::npos) { err = "the formula contains a stray character"; return false; }
    Compiler c(src, vars, code_);
    bool ok = c.ternary();
    if (ok) { c.ws(); if (*c.p) ok = c.fail(std::string("unexpected '") + *c.p + "'"); }
    if (ok && c.maxDepth > MAX_STACK) ok = c.fail("the formula is too complicated");
    if (ok && code_.size() > MAX_INS) ok = c.fail("the formula is too long");
    if (!ok) { err = c.err; code_.clear(); return false; }
    return true;
}

// ---------------------------------------------------------------- evaluation
double Program::eval(const double* v) const {
    auto truthy = [](double x) { return std::isfinite(x) && x != 0.0; };
    auto safeMod = [](double a, double b) -> double {
        if (!std::isfinite(a) || !std::isfinite(b) || b == 0.0) return 0.0;
        double m = std::fmod(a, b);
        if (m != 0.0 && ((m < 0.0) != (b < 0.0))) m += b;
        return std::isfinite(m) ? m : 0.0;
    };
    double st[MAX_STACK + 4];
    int sp = 0;
    for (const Ins& i : code_) {
        switch (i.op) {
            case PUSH: st[sp++] = i.val; break;
            case LOAD: st[sp++] = v[i.a]; break;
            case NEG: st[sp - 1] = -st[sp - 1]; break;
            case NOT: st[sp - 1] = !truthy(st[sp - 1]) ? 1.0 : 0.0; break;
            case ADD: --sp; st[sp - 1] += st[sp]; break;
            case SUB: --sp; st[sp - 1] -= st[sp]; break;
            case MUL: --sp; st[sp - 1] *= st[sp]; break;
            case DIV: --sp; st[sp - 1] = st[sp] != 0.0 ? st[sp - 1] / st[sp] : 0.0; break;       // dividing by zero gives 0, never a crash
            case MOD: --sp; st[sp - 1] = safeMod(st[sp - 1], st[sp]); break;
            case LT: --sp; st[sp - 1] = st[sp - 1] < st[sp]; break;
            case GT: --sp; st[sp - 1] = st[sp - 1] > st[sp]; break;
            case LE: --sp; st[sp - 1] = st[sp - 1] <= st[sp]; break;
            case GE: --sp; st[sp - 1] = st[sp - 1] >= st[sp]; break;
            case EQ: --sp; st[sp - 1] = st[sp - 1] == st[sp]; break;
            case NE: --sp; st[sp - 1] = st[sp - 1] != st[sp]; break;
            case AND: --sp; st[sp - 1] = (truthy(st[sp - 1]) && truthy(st[sp])) ? 1.0 : 0.0; break;
            case OR: --sp; st[sp - 1] = (truthy(st[sp - 1]) || truthy(st[sp])) ? 1.0 : 0.0; break;
            case SEL: sp -= 2; st[sp - 1] = truthy(st[sp - 1]) ? st[sp] : st[sp + 1]; break;
            case CALL: {
                switch (i.a) {
                    case FN_SIN: st[sp - 1] = std::sin(st[sp - 1]); break;
                    case FN_COS: st[sp - 1] = std::cos(st[sp - 1]); break;
                    case FN_TAN: st[sp - 1] = std::tan(st[sp - 1]); break;
                    case FN_ATAN2: --sp; st[sp - 1] = std::atan2(st[sp - 1], st[sp]); break;
                    case FN_ABS: st[sp - 1] = std::fabs(st[sp - 1]); break;
                    case FN_SQRT: st[sp - 1] = st[sp - 1] > 0 ? std::sqrt(st[sp - 1]) : 0.0; break;
                    case FN_POW: --sp; { double r = std::pow(st[sp - 1], st[sp]); st[sp - 1] = std::isfinite(r) ? r : 0.0; } break;
                    case FN_EXP: { double r = std::exp(std::min(st[sp - 1], 50.0)); st[sp - 1] = r; } break;
                    case FN_LOG: st[sp - 1] = st[sp - 1] > 0 ? std::log(st[sp - 1]) : 0.0; break;
                    case FN_FLOOR: st[sp - 1] = std::floor(st[sp - 1]); break;
                    case FN_CEIL: st[sp - 1] = std::ceil(st[sp - 1]); break;
                    case FN_ROUND: st[sp - 1] = std::round(st[sp - 1]); break;
                    case FN_FRACT: st[sp - 1] -= std::floor(st[sp - 1]); break;
                    case FN_SIGN: st[sp - 1] = st[sp - 1] > 0 ? 1.0 : st[sp - 1] < 0 ? -1.0 : 0.0; break;
                    case FN_MIN: --sp; st[sp - 1] = std::min(st[sp - 1], st[sp]); break;
                    case FN_MAX: --sp; st[sp - 1] = std::max(st[sp - 1], st[sp]); break;
                    case FN_CLAMP: sp -= 2; st[sp - 1] = std::min(std::max(st[sp - 1], st[sp]), st[sp + 1]); break;
                    case FN_MIX: sp -= 2; st[sp - 1] = st[sp - 1] + (st[sp] - st[sp - 1]) * st[sp + 1]; break;
                    case FN_SMOOTH: {
                        sp -= 2;
                        const double a = st[sp - 1], b = st[sp], x = st[sp + 1];
                        double t = b != a ? (x - a) / (b - a) : (x >= b ? 1.0 : 0.0);
                        t = t < 0 ? 0 : t > 1 ? 1 : t;
                        st[sp - 1] = t * t * (3 - 2 * t);
                    } break;
                    case FN_STEP: --sp; st[sp - 1] = st[sp] >= st[sp - 1] ? 1.0 : 0.0; break;
                    case FN_HASH: st[sp - 1] = hash01(st[sp - 1]); break;
                    case FN_MOD: --sp; st[sp - 1] = safeMod(st[sp - 1], st[sp]); break;
                    default: break;
                }
            } break;
        }
    }
    const double r = sp > 0 ? st[sp - 1] : 0.0;
    return std::isfinite(r) ? r : 0.0;                      // an overflow (1e300 * 1e300) must not reach the drawing code as infinity or NaN
}

} // namespace expr
