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

z3::expr combinedSimplify(const z3::expr & expr)
{
    z3::context & ctx = expr.ctx();

    z3::params simplify_p(ctx);
    simplify_p.set("elim_and", true);
    simplify_p.set("flat_and_or", true);
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
