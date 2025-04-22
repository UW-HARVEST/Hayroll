#ifndef HAYROLL_UTIL_HPP
#define HAYROLL_UTIL_HPP

#ifndef __GLIBCXX__
#error "Not using libstdc++"
#endif
#if __GLIBCXX__ < 20220719
#error "libstdc++ version is too old, require GCC 13 or above"
#endif

#define DEBUG (!NDEBUG)

#include <string>
#include <vector>
#include <filesystem>

#include <z3++.h>

namespace Hayroll
{

template<class... Ts>
struct overloaded : Ts... { using Ts::operator()...; };
template<class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;

// A string builder that can append std::string, std::string_view, and const char *
// Reduces copys at best effort
class StringBuilder
{
public:
    StringBuilder() : bufferSize(0) {}

    // Take ownership of the string
    void append(std::string && str)
    {
        bufferOwnershipCache.emplace_back(std::move(str));
        buffer.emplace_back(bufferOwnershipCache.back());
        bufferSize += bufferOwnershipCache.back().size();
    }

    // Use only reference to the string
    void append(std::string_view str)
    {
        buffer.emplace_back(str);
        bufferSize += str.size();
    }

    // Use only reference to the string (implicitly converted to std::string_view)
    void append(const char * str)
    {
        buffer.emplace_back(str);
        bufferSize += std::char_traits<char>::length(str);
    }

    // Concatenate all segments into a single string
    std::string str() const
    {
        std::string result;
        result.reserve(bufferSize + 32);
        for (std::string_view segment : buffer)
        {
            result.append(segment);
        }
        return result;
    }
private:
    std::vector<std::string> bufferOwnershipCache;
    std::vector<std::string_view> buffer;
    size_t bufferSize;
};

bool isAllWhitespace(const std::string & s)
{
    return s.find_first_not_of(" \t\n\v\f\r") == std::string::npos;
}

// In support of std::unordered_map<std::string, T> lookup using std::string_view
struct TransparentStringHash
{
    using is_transparent = void;

    size_t operator()(const std::string & s) const
    {
        return std::hash<std::string>{}(s);
    }
    size_t operator()(std::string_view sv) const
    {
        return std::hash<std::string_view>{}(sv);
    }
};

// In support of std::unordered_map<std::string, T> lookup using std::string_view
struct TransparentStringEqual
{
    using is_transparent = void;

    bool operator()(const std::string & lhs, const std::string & rhs) const
    {
        return lhs == rhs;
    }
    bool operator()(const std::string & lhs, std::string_view rhs) const
    {
        return lhs == rhs;
    }
    bool operator()(std::string_view lhs, const std::string & rhs) const
    {
        return lhs == rhs;
    }
    bool operator()(std::string_view lhs, std::string_view rhs) const
    {
        return lhs == rhs;
    }
};

z3::expr ctxSolverSimplify(const z3::expr & expr)
{
    z3::context & ctx = expr.ctx();
    z3::goal g(ctx);
    g.add(expr);
    z3::apply_result res = z3::tactic(ctx, "ctx-solver-simplify")(g);
    assert(res.size() > 0);
    return res[0].as_expr();
}

z3::expr tryTrueFalseSimplify(const z3::expr & expr)
{
    z3::context & ctx = expr.ctx();
    z3::solver s(ctx);
    s.add(!expr);
    if (s.check() == z3::unsat) return ctx.bool_val(true);
    s.reset();
    s.add(expr);
    if (s.check() == z3::unsat) return ctx.bool_val(false);
    return expr;
}

// LLM-written function. Needs refactoring. 
// Factor boolean expressions:
// (x && y) || (x && z) => x && (y || z)
// (x || y) && (x || z) => x || (y && z)
z3::expr factorCommon(const z3::expr & e) {
    z3::context &ctx = e.ctx();

    // If application (compound) expression:
    if (e.is_app()) {
        // Handle disjunction of conjunctions: (or (and ...) (and ...) ...)
        if (e.is_or()) {
            z3::expr_vector ors(ctx);
            for (const auto &a : e.args()) {
                ors.push_back(factorCommon(a));  // recurse on children
            }

            // Check all disjuncts are conjunctions
            bool all_and = true;
            for (const auto &a : ors) {
                if (!a.is_and()) { all_and = false; break; }
            }

            // Factor if at least two conjunctions share common literals
            if (all_and && ors.size() > 1) {
                // Initialize common literals from first conjunction
                std::vector<z3::expr> common;
                for (unsigned i = 0; i < ors[0].num_args(); ++i)
                    common.push_back(ors[0].arg(i));

                // Intersect with rest
                for (std::size_t i = 1; i < ors.size(); ++i) {
                    std::vector<z3::expr> tmp;
                    for (auto &c : common) {
                        for (const auto &d : ors[i].args()) {
                            if (c.hash() == d.hash()) {
                                tmp.push_back(c);
                                break;
                            }
                        }
                    }
                    common.swap(tmp);
                    if (common.empty()) break;
                }

                if (!common.empty()) {
                    // Build residuals: remove common from each conjunction
                    z3::expr_vector residuals(ctx);
                    for (const auto &a : ors) {
                        z3::expr_vector rest(ctx);
                        for (const auto &lit : a.args()) {
                            // keep literals not in common
                            if (std::none_of(common.begin(), common.end(),
                                             [&](auto &c){ return c.hash()==lit.hash(); })) {
                                rest.push_back(lit);
                            }
                        }
                        // empty => true, single => itself, else => and(...)
                        residuals.push_back(
                            rest.empty()        ? ctx.bool_val(true)
                            : rest.size()==1    ? rest[0]
                                                : mk_and(rest));
                    }
                    z3::expr or_rest = (residuals.size()==1 ? residuals[0] : mk_or(residuals));

                    // Combine common literals with factored rest
                    z3::expr_vector common_exprs(ctx);
                    for (auto &c : common) common_exprs.push_back(c);
                    return z3::mk_and(common_exprs) && tryTrueFalseSimplify(or_rest);
                }
            }
            // Default: unreduced or(...)
            return z3::mk_or(ors);
        }
        // Handle conjunction of disjunctions: dual case for (and (or...) (or...) ...)
        else if (e.is_and()) {
            z3::expr_vector ands(ctx);
            for (const auto &a : e.args())
                ands.push_back(factorCommon(a));

            bool all_or = true;
            for (const auto &a : ands) {
                if (!a.is_or()) { all_or = false; break; }
            }

            if (all_or && ands.size() > 1) {
                std::vector<z3::expr> common;
                for (unsigned i = 0; i < ands[0].num_args(); ++i)
                    common.push_back(ands[0].arg(i));

                for (std::size_t i = 1; i < ands.size(); ++i) {
                    std::vector<z3::expr> tmp;
                    for (auto &c : common) {
                        for (const auto &d : ands[i].args()) {
                            if (c.hash() == d.hash()) {
                                tmp.push_back(c);
                                break;
                            }
                        }
                    }
                    common.swap(tmp);
                    if (common.empty()) break;
                }

                if (!common.empty()) {
                    z3::expr_vector residuals(ctx);
                    for (const auto &a : ands) {
                        z3::expr_vector rest(ctx);
                        for (const auto &lit : a.args()) {
                            if (std::none_of(common.begin(), common.end(),
                                             [&](auto &c){ return c.hash()==lit.hash(); })) {
                                rest.push_back(lit);
                            }
                        }
                        residuals.push_back(
                            rest.empty()        ? ctx.bool_val(true)
                            : rest.size()==1    ? rest[0]
                                                : mk_or(rest));
                    }
                    z3::expr and_rest = (residuals.size()==1 ? residuals[0] : mk_and(residuals));

                    z3::expr_vector common_exprs(ctx);
                    for (auto &c : common) common_exprs.push_back(c);
                    return mk_or(common_exprs) || tryTrueFalseSimplify(and_rest);
                }
            }
            return z3::mk_and(ands);
        }
        // Rebuild other n-ary applications
        else {
            z3::func_decl d = e.decl();
            unsigned n = e.num_args();
            std::vector<Z3_ast> args(n);
            for (unsigned i = 0; i < n; ++i)
                args[i] = factorCommon(e.arg(i)).operator Z3_ast();
            return z3::expr(ctx, Z3_mk_app(ctx, d, n, args.data()));
        }
    }
    // Base case: literal or variable
    return e;
}

// z3::expr factorCommon(const z3::expr & e) {
//     z3::context &ctx = e.ctx();
//     if (e.is_app()) {
//         if (e.is_or()) {
//             z3::expr_vector ors(ctx);
//             for (const auto & a : e.args())
//                 ors.push_back(factorCommon(a));

//             bool all_and = true;
//             for (const auto & a : ors)
//                 if (!a.is_and()) { all_and = false; break; }

//             if (all_and && ors.size() > 1) {
//                 std::vector<z3::expr> common;
//                 for (unsigned i = 0; i < ors[0].num_args(); ++i) {
//                     common.push_back(ors[0].arg(i));
//                 }
//                 for (std::size_t i = 1; i < ors.size(); ++i) {
//                     std::vector<z3::expr> tmp;
//                     for (auto &c : common) {
//                         for (const auto &d : ors[i].args())
//                             if (c.hash() == d.hash()) { tmp.push_back(c); break; }
//                     }
//                     common.swap(tmp);
//                     if (common.empty()) break;
//                 }
//                 if (!common.empty()) {
//                     z3::expr_vector residuals(ctx);
//                     for (const auto &a : ors) {
//                         z3::expr_vector rest(ctx);
//                         for (const auto &lit : a.args())
//                             if (std::none_of(common.begin(), common.end(),
//                                              [&](auto &c){ return c.hash()==lit.hash(); }))
//                                 rest.push_back(lit);
//                         residuals.push_back(
//                           rest.empty() ? ctx.bool_val(true)
//                                        : rest.size()==1 ? rest[0]
//                                                         : mk_and(rest));
//                     }
//                     z3::expr or_rest = residuals.size()==1 ? residuals[0] : mk_or(residuals);
//                     return common.size()==1
//                          ? common[0] && or_rest
//                          : [&]() {
//                                z3::expr_vector common_exprs(ctx);
//                                for (const auto &c : common) {
//                                    common_exprs.push_back(c);
//                                }
//                                return mk_and(common_exprs) && or_rest;
//                            }();
//                 }
//             }
//             return mk_or(ors);
//         }
//         else {
//             z3::func_decl d = e.decl();
//             unsigned n = e.num_args();
//             std::vector<Z3_ast> args(n);
//             for (unsigned i = 0; i < n; ++i)
//                 args[i] = factorCommon(e.arg(i)).operator Z3_ast();
//             return z3::expr(ctx, Z3_mk_app(ctx, d, n, args.data()));
//         }
//     }
//     return e;
// }

z3::expr combinedSimplify(const z3::expr & expr)
{
    z3::context & ctx = expr.ctx();

    z3::params simplify_p(ctx);
    // simplify_p.set("elim_and", true);
    // simplify_p.set("flat_and_or", true);
    z3::tactic simplify_t = with(z3::tactic(ctx, "simplify"), simplify_p);

    z3::tactic t =
        simplify_t &
        z3::tactic(ctx, "propagate-values") &
        z3::tactic(ctx, "aig") &
        z3::tactic(ctx, "cofactor-term-ite") &
        z3::tactic(ctx, "ctx-solver-simplify") &
        simplify_t
    ;
    z3::goal g(ctx);
    g.add(expr);
    
    z3::apply_result res = t(g);
    
    assert(res.size() != 0);
    return res[0].as_expr();
}

// WIP
z3::expr simplifyOrOfAnd(const z3::expr & expr)
{
    z3::context & ctx = expr.ctx();

    z3::params simp_p(ctx);
    simp_p.set("flat_and_or", true);
    simp_p.set("bv_ite2id", true);
    simp_p.set("local_ctx", true);
    // simp_p.set("elim_and",    true);
    // simp_p.set("elim_ite",    true);
    z3::tactic simplify_t = with(z3::tactic(ctx, "simplify"), simp_p);

    z3::tactic t =
        simplify_t
        & z3::tactic(ctx, "propagate-values")
        & z3::tactic(ctx, "unit-subsume-simplify")
        & z3::tactic(ctx, "aig")
        & z3::tactic(ctx, "nnf")
        & z3::tactic(ctx, "ctx-solver-simplify")
        // & z3::tactic(ctx, "dom-simplify")
        // & z3::tactic(ctx, "simplify")
        & simplify_t
        ;

    z3::goal g(ctx);
    g.add(expr);
    z3::apply_result res = t(g);
    assert(res.size() > 0);
    z3::expr resExpr = res[0].as_expr();

    z3::expr expr2 = factorCommon(resExpr);

    // Check that resExpr is equivalent to expr
    #if DEBUG
        z3::solver s(ctx);
        s.add(expr);
        s.add(!resExpr);
        assert(s.check() == z3::unsat);
    #endif

    // Do a second pass of simplification. This time do not do aig. 
    
    return ctxSolverSimplify(expr2);
}

z3::check_result z3Check(const z3::expr & expr)
{
    z3::context & ctx = expr.ctx();
    z3::solver solver(ctx);
    solver.add(expr);
    z3::check_result result = solver.check();
    assert(result == z3::sat || result == z3::unsat);
    return result;
}

} // namespace Hayroll

#endif // HAYROLL_UTIL_HPP
