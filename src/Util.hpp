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
#include <fstream>
#include <algorithm>

#include <spdlog/spdlog.h>
#include <z3++.h>

namespace Hayroll
{

const std::filesystem::path C2RustExe = std::filesystem::canonical(std::filesystem::path(C2RUST_EXE));
const std::filesystem::path ClangExe = std::filesystem::canonical(std::filesystem::path(CLANG_EXE));
const std::filesystem::path MakiDir = std::filesystem::canonical(std::filesystem::path(MAKI_DIR));
const std::filesystem::path LibmcsDir = std::filesystem::canonical(std::filesystem::path(LIBMCS_DIR));
const std::filesystem::path HayrollReaperExe = std::filesystem::canonical(std::filesystem::path(HAYROLL_REAPER_EXE));

template<class... Ts>
struct overloaded : Ts... { using Ts::operator()...; };
template<class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;

std::string loadFileToString(const std::filesystem::path & path)
{
    std::ifstream file(path);
    if (!file.is_open())
    {
        throw std::runtime_error("Error: Could not open file " + path.string());
    }
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();
    return content;
}

void saveStringToFile(std::string_view content, const std::filesystem::path & path)
{
    // Create parent directories if they do not exist
    std::filesystem::create_directories(path.parent_path());

    // Warn if writing into LibmcsDir/libm
    if (path.string().starts_with((LibmcsDir / "libm").string()))
    {
        SPDLOG_WARN("Writing into LibmcsDir/libm: {}", path.string());
    }

    std::ofstream file(path);
    if (!file.is_open())
    {
        throw std::runtime_error("Error: Could not open file " + path.string());
    }
    file << content;
    file.close();
}

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

// In support of std::unordered_map<z3::expr, T>
struct Z3ExprHash
{
    size_t operator()(const z3::expr & expr) const
    {
        return expr.hash();
    }
};

// In support of std::unordered_map<z3::expr, T>
struct Z3ExprEqual
{
    bool operator()(const z3::expr & lhs, const z3::expr & rhs) const
    {
        return z3::eq(lhs, rhs);
    }
};

z3::check_result z3Check(const z3::expr & expr)
{
    z3::context & ctx = expr.ctx();
    z3::solver solver(ctx);
    solver.add(expr);
    z3::check_result result = solver.check();
    assert(result == z3::sat || result == z3::unsat);
    return result;
}

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
    if (z3Check(!expr) == z3::unsat)
    {
        return expr.ctx().bool_val(true);
    }
    if (z3Check(expr) == z3::unsat)
    {
        return expr.ctx().bool_val(false);
    }
    return expr;
}

// Factor boolean expressions:
// (x && y) || (x && z) => x && (y || z)
// (x || y) && (x || z) => x || (y && z)
z3::expr factorCommonTerm(const z3::expr & e)
{
    z3::context &ctx = e.ctx();

    // If not application (compound) expression:
    if (!e.is_app()) return e;
    if (e.is_or()) // Handle disjunction of conjunctions: (or (and ...) (and ...) ...)
    {
        z3::expr_vector ors(ctx);
        for (const z3::expr & a : e.args())
        {
            ors.push_back(factorCommonTerm(a));
        }

        // Check all disjuncts are conjunctions
        bool allAnd = true;
        for (const z3::expr & a : ors)
        {
            if (!a.is_and())
            {
                allAnd = false;
                break;
            }
        }

        // Factor if at least two conjunctions share common literals
        if (allAnd && ors.size() > 1)
        {
            // Initialize common literals from first conjunction
            std::vector<z3::expr> common;
            for (unsigned i = 0; i < ors[0].num_args(); ++i)
                common.push_back(ors[0].arg(i));

            // Intersect with rest
            for (std::size_t i = 1; i < ors.size(); ++i)
            {
                std::vector<z3::expr> tmp;
                for (z3::expr & c : common)
                {
                    for (const z3::expr &d : ors[i].args())
                    {
                        if (z3::eq(c, d))
                        {
                            tmp.push_back(c);
                            break;
                        }
                    }
                }
                common.swap(tmp);
                if (common.empty()) break;
            }

            if (!common.empty())
            {
                // Build residuals: remove common from each conjunction
                z3::expr_vector residuals(ctx);
                for (const z3::expr & a : ors)
                {
                    z3::expr_vector rest(ctx);
                    for (const z3::expr & lit : a.args())
                    {
                        // keep literals not in common
                        if
                        (
                            std::none_of
                            (
                                common.begin(),
                                common.end(),
                                [&](auto &c){ return z3::eq(c, lit); }
                            )
                        )
                        {
                            rest.push_back(lit);
                        }
                    }
                    // empty => true, single => itself, else => and(...)
                    if (rest.empty())
                    {
                        residuals.push_back(ctx.bool_val(true));
                    }
                    else if (rest.size() == 1)
                    {
                        residuals.push_back(rest[0]);
                    }
                    else
                    {
                        residuals.push_back(mk_and(rest));
                    }
                }
                z3::expr or_rest = (residuals.size() == 1 ? residuals[0] : mk_or(residuals));

                // Combine common literals with factored rest
                z3::expr_vector common_exprs(ctx);
                for (z3::expr & c : common) common_exprs.push_back(c);
                return z3::mk_and(common_exprs) && tryTrueFalseSimplify(or_rest);
            }
            else return z3::mk_or(ors);
        }
        // Default: unreduced or(...)
        return z3::mk_or(ors);
    }
    // Handle conjunction of disjunctions: dual case for (and (or...) (or...) ...)
    else if (e.is_and())
    {
        z3::expr_vector ands(ctx);
        for (const z3::expr & a : e.args())
        {
            ands.push_back(factorCommonTerm(a));
        }

        bool allOr = true;
        for (const z3::expr & a : ands)
        {
            if (!a.is_or())
            {
                allOr = false;
                break;
            }
        }

        if (allOr && ands.size() > 1)
        {
            std::vector<z3::expr> common;
            for (unsigned i = 0; i < ands[0].num_args(); ++i)
                common.push_back(ands[0].arg(i));

            for (std::size_t i = 1; i < ands.size(); ++i)
            {
                std::vector<z3::expr> tmp;
                for (z3::expr & c : common)
                {
                    for (const z3::expr & d : ands[i].args())
                    {
                        if (z3::eq(c, d))
                        {
                            tmp.push_back(c);
                            break;
                        }
                    }
                }
                common.swap(tmp);
                if (common.empty()) break;
            }

            if (!common.empty())
            {
                z3::expr_vector residuals(ctx);
                for (const z3::expr & a : ands)
                {
                    z3::expr_vector rest(ctx);
                    for (const z3::expr & lit : a.args())
                    {
                        if 
                        (
                            std::none_of
                            (
                                common.begin(),
                                common.end(),
                                [&](auto &c){ return z3::eq(c, lit); }
                            )
                        )
                        {
                            rest.push_back(lit);
                        }
                    }
                    if (rest.empty())
                    {
                        residuals.push_back(ctx.bool_val(true));
                    }
                    else if (rest.size() == 1)
                    {
                        residuals.push_back(rest[0]);
                    }
                    else
                    {
                        residuals.push_back(mk_or(rest));
                    }
                }
                z3::expr and_rest = (residuals.size()==1 ? residuals[0] : mk_and(residuals));

                z3::expr_vector common_exprs(ctx);
                for (z3::expr & c : common) common_exprs.push_back(c);
                return mk_or(common_exprs) || tryTrueFalseSimplify(and_rest);
            }
            else return mk_and(ands);
        }
        return mk_and(ands);
    }
    else // Rebuild other n-ary applications
    {
        z3::func_decl d = e.decl();
        unsigned n = e.num_args();
        std::vector<Z3_ast> args(n);
        for (unsigned i = 0; i < n; ++i)
        {
            args[i] = factorCommonTerm(e.arg(i));
        }
        return z3::expr(ctx, Z3_mk_app(ctx, d, n, args.data()));
    }
}

// Simplify expressions
// Most effective for the form: (x && y) || (x && z) => x && (y || z) => x (y || z == 1)
// Does more setup and cleanup work than the factorCommonTerm function
z3::expr simplifyOrOfAnd(const z3::expr & expr)
{
    z3::context & ctx = expr.ctx();
    
    z3::params paramsSimplify(ctx);
    paramsSimplify.set("flat_and_or", true);
    paramsSimplify.set("bv_ite2id", true);
    paramsSimplify.set("local_ctx", true);
    z3::tactic tacticSimplify = with(z3::tactic(ctx, "simplify"), paramsSimplify);

    // Flat and/or before sending to factorCommonTerm
    z3::goal goal1(ctx);
    goal1.add(expr);
    z3::apply_result res1 = tacticSimplify(goal1);
    assert(res1.size() > 0);
    z3::expr expr1 = res1[0].as_expr();

    z3::expr expr2 = factorCommonTerm(expr1);

    z3::tactic tacticCompound =
        tacticSimplify
        & z3::tactic(ctx, "propagate-values")
        & z3::tactic(ctx, "unit-subsume-simplify")
        // & z3::tactic(ctx, "aig")
        // & z3::tactic(ctx, "nnf")
        & z3::tactic(ctx, "dom-simplify")
        & z3::tactic(ctx, "ctx-solver-simplify")
        // & z3::tactic(ctx, "simplify")
        & tacticSimplify
    ;

    z3::goal goal2(ctx);
    goal2.add(expr2);
    z3::apply_result res2 = tacticCompound(goal2);
    assert(res2.size() > 0);
    z3::expr expr3 = res2[0].as_expr();

    // Check that resExpr is equivalent to expr
    assert(z3Check(expr3 != expr) == z3::unsat);
    
    return expr3;
}

} // namespace Hayroll

#endif // HAYROLL_UTIL_HPP
