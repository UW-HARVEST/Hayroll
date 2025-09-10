#ifndef HAYROLL_DEFINESET_HPP
#define HAYROLL_DEFINESET_HPP

#include <optional>
#include <unordered_map>

#include <z3++.h>

#include <spdlog/spdlog.h>

#include "Util.hpp"

namespace Hayroll
{

struct DefineSet
{
    z3::model model;
    std::unordered_map<std::string, std::optional<int>> defines;

    DefineSet() = delete;

    DefineSet(const z3::model & model)
        : model(model)
    {
        for (unsigned i = 0; i < model.size(); i++)
        {
            z3::func_decl v = model[i];
            assert(v.arity() == 0); // only constants
            std::string z3VarName = v.name().str();
            std::string prefix = z3VarName.substr(0, 3); // "def" or "val"
            std::string name = z3VarName.substr(3);
            z3::expr value = model.get_const_interp(v);
            if (prefix == "val")
            {
                int intValue = value.get_numeral_int();
                defines.emplace(name, intValue);
            }
            else if (prefix == "def")
            {
                bool boolValue = z3::eq(value, model.ctx().bool_val(true));
                if (boolValue) defines.emplace(name, std::nullopt);
            }
            else assert(false);
        }
    }

    std::string toString() const
    {
        std::string buff;
        for (const auto & [name, val] : defines)
        {
            if (!val.has_value())
            {
                buff += std::format("-D{} ", name);
            }
            else
            {
                buff += std::format("-D{}={} ", name, val.value());
            }
        }
        return buff;
    }

    bool satisfies(const z3::expr & expr) const
    {
        return z3CheckTautology(model.eval(expr));
    }
};

} // namespace Hayroll

#endif // HAYROLL_DEFINESET_HPP
