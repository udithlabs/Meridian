// Meridian: a tiny expression language used by theme packs (see themes/README.txt).
//
// A theme file can put a formula wherever it would put a number, for example
//
//     "x": "0.5 + 0.012 * sin(t * 0.35)"          a layer that drifts left and right
//     "when": "release_days < 20"                  show something only in the last 20 days
//     "opacity": "0.6 * (1 - gloom)"               fade out when the weather is heavy
//
// This file is portable C++ (no Windows code), so it can be tested on its own.
//
// HOW IT WORKS
//   1. A VarTable lists every variable name an expression may use (t, W, H, gloom ... plus the theme's own).
//   2. Program::compile() turns the text into a short list of stack-machine steps (reverse Polish notation).
//   3. Program::eval() runs those steps against an array of numbers, one slot per variable. It is fast enough to run
//      for every layer of every frame.
//
// SYNTAX (like most languages)
//   numbers      1   0.25   .5   1e-3
//   operators    + - * / %   (mod)      comparisons  < > <= >= == !=      logic  && || !      choice  a ? b : c
//   functions    sin cos tan atan2 abs sqrt pow exp log floor ceil round fract sign
//                min max clamp(x,lo,hi) mix(a,b,t) smoothstep(a,b,x) step(edge,x) hash(n) mod(a,b)
//   constants    pi
//   Comparisons give 1 (true) or 0 (false), so "a > b" can also be used as a number: "0.4 * (hour > 18)".
#pragma once
#include <string>
#include <vector>
#include <unordered_map>

namespace expr {

struct VarTable {
    std::vector<std::string> names;
    std::unordered_map<std::string, int> index;
    int add(const std::string& n) {                    // slot of a name, creating it when new
        auto it = index.find(n);
        if (it != index.end()) return it->second;
        names.push_back(n); index[n] = (int)names.size() - 1; return (int)names.size() - 1;
    }
    int find(const std::string& n) const {             // -1 when unknown
        auto it = index.find(n);
        return it == index.end() ? -1 : it->second;
    }
};

class Program {
public:
    // Compiles 'src' against 'vars'. A name that is not in the table is an error (so typos are reported, not silently 0).
    bool compile(const std::string& src, const VarTable& vars, std::string& err);
    double eval(const double* vars) const;             // vars[slot] for every slot of the table used at compile time
    bool isConst() const { return code_.size() == 1 && code_[0].op == PUSH; }
    double constValue() const { return code_.empty() ? 0.0 : code_[0].val; }
    bool uses(int slot) const {                        // does the formula read this variable? (used to tell whether a theme animates)
        for (const Ins& i : code_) if (i.op == LOAD && i.a == slot) return true;
        return false;
    }

private:
    enum Op : unsigned char { PUSH, LOAD, NEG, NOT, ADD, SUB, MUL, DIV, MOD, LT, GT, LE, GE, EQ, NE, AND, OR, SEL, CALL };
    struct Ins { Op op; int a = 0; double val = 0; };   // a: variable slot, or function id
    std::vector<Ins> code_;
    friend struct Compiler;
};

} // namespace expr
