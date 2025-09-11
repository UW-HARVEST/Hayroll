#ifndef HAYROLL_SPLITTER_HPP
#define HAYROLL_SPLITTER_HPP

#include <string>

#include <z3++.h>

#include <spdlog/spdlog.h>

#include "Util.hpp"
#include "DefineSet.hpp"
#include "PremiseTree.hpp"
#include "CompileCommand.hpp"
#include "C2RustWrapper.hpp"
#include "RewriteIncludesWrapper.hpp"
#include "TempDir.hpp"

namespace Hayroll
{

struct Splitter
{
public:
    static std::vector<DefineSet> run
    (
        const PremiseTree * premiseTree,
        const CompileCommand & compileCommand
    )
    {
        std::vector<DefineSet> result;
        if (!premiseTree) return result;
        // Premise tree nodes that are not yet satisfied by any DefineSet.
        // Do reverse-level-order traversal so that more constrained nodes are processed first.
        std::list<const PremiseTree *> worklist = premiseTree->getDescendantsLevelOrder();

        while (!worklist.empty())
        {
            const PremiseTree * node = worklist.back();
            worklist.pop_back();
            z3::expr premise = node->getCompletePremise();
            SPDLOG_TRACE("Processing premise tree node: {}", node->toString());
            SPDLOG_TRACE("Complete premise: {}", premise.to_string());

            DefineSet defineSet = node->getDefineSet();
            SPDLOG_TRACE("Created new DefineSet: {}", defineSet.toString());
            result.push_back(defineSet);

            // Check that the DefineSet can be transpiled by C2Rust
            if (!checkTranspilable(defineSet, compileCommand))
            {
                SPDLOG_ERROR("DefineSet cannot be transpiled by C2Rust: {}", defineSet.toString());
                throw std::runtime_error("DefineSet cannot be transpiled by C2Rust");
            }

            // Remove all nodes that are satisfied by the new DefineSet
            for (auto it = worklist.begin(); it != worklist.end(); )
            {
                const PremiseTree * otherNode = *it;
                z3::expr otherPremise = otherNode->getCompletePremise();
                if (defineSet.satisfies(otherPremise))
                {
                    SPDLOG_TRACE("DefineSet {} satisfies premise tree node {}, removing it from worklist.",
                                 defineSet.toString(),
                                 otherNode->toString());
                    it = worklist.erase(it);
                }
                else
                {
                    ++it;
                }
            }
        }
        return result;
    }

private:
    static bool checkTranspilable
    (
        const DefineSet & defineSet,
        const CompileCommand & compileCommand
    )
    {
        CompileCommand updatedCommand = compileCommand.withUpdatedDefineSet(defineSet);
        try
        {
            std::string cuStr = RewriteIncludesWrapper::runRewriteIncludes(updatedCommand);
            std::string rsStr = C2RustWrapper::runC2Rust(cuStr, updatedCommand);
        }
        catch (const std::exception & e)
        {
            SPDLOG_WARN("C2Rust transpilation failed: {}", e.what());
            return false;
        }
        return true;
    }
};

} // namespace Hayroll

#endif // HAYROLL_SPLITTER_HPP
