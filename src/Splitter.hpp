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
#include "MakiWrapper.hpp"
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

        result.push_back(DefineSet{}); // Always have the empty DefineSet.

        // Premise tree nodes that are not yet satisfied by any DefineSet.
        // Do reverse-level-order traversal so that more constrained nodes are processed first.
        std::list<const PremiseTree *> worklist = premiseTree->getDescendantsLevelOrder();
        std::list<const PremiseTree *> uncovered;

        while (!worklist.empty())
        {
            const PremiseTree * node = worklist.back();
            worklist.pop_back();
            z3::expr premise = node->getCompletePremise();
            SPDLOG_TRACE("Processing premise tree node: {}", node->toString());
            SPDLOG_TRACE("Complete premise: {}", premise.to_string());

            DefineSet defineSet = node->getDefineSet();
            
            // Check that the DefineSet can be transpiled by C2Rust
            if (!validate(defineSet, compileCommand))
            {
                // If not, skip this DefineSet
                SPDLOG_WARN("DefineSet {} cannot be validated by C2Rust or Maki, skipping.",
                    defineSet.toString());
                uncovered.push_back(node);
                continue;
            }

            SPDLOG_TRACE("Created new DefineSet: {}", defineSet.toString());
            result.push_back(defineSet);

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

        SPDLOG_DEBUG("Generated {} DefineSet(s).", result.size());
        if (!uncovered.empty())
        {
            SPDLOG_DEBUG("The following premise tree nodes could not be covered by any valid DefineSet:");
            for (const PremiseTree * node : uncovered)
            {
                SPDLOG_DEBUG(" - Node: {}", node->toString());
                SPDLOG_DEBUG("   Premise: {}", node->getCompletePremise().to_string());
            }
        }

        return result;
    }

private:
    // Check if the DefineSet can be used to successfully run through C2Rust.
    static bool validate
    (
        const DefineSet & defineSet,
        const CompileCommand & compileCommand
    )
    {
        CompileCommand updatedCommand = compileCommand.withUpdatedDefineSet(defineSet);
        try
        {
            std::string cuStr = RewriteIncludesWrapper::runRewriteIncludes(updatedCommand);
            auto [rsStr, cargoToml] = C2RustWrapper::transpile(cuStr, updatedCommand);
        }
        catch (const std::exception & e)
        {
            SPDLOG_WARN("C2Rust transpilation failed during DefineSet validation: {}", defineSet.toString());
            return false;
        }

        // Validating against Maki is too time-consuming, so we offload the responsibility to the caller

        return true;
    }
};

} // namespace Hayroll

#endif // HAYROLL_SPLITTER_HPP
