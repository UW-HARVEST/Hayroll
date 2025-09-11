#ifndef HAYROLL_PREMISETREE_HPP
#define HAYROLL_PREMISETREE_HPP

#include <memory>
#include <variant>
#include <unordered_map>
#include <list>
#include <queue>

#include <z3++.h>

#include <spdlog/spdlog.h>

#include "Util.hpp"
#include "IncludeTree.hpp"
#include "TreeSitter.hpp"
#include "MakiWrapper.hpp"
#include "DefineSet.hpp"

namespace Hayroll
{

struct PremiseTree;
using PremiseTreePtr = std::unique_ptr<PremiseTree>;
using ConstPremiseTreePtr = std::unique_ptr<const PremiseTree>;

// A tree that keeps track of the premises of an #if/#else body or a macro expansion in C code.
// A translation_unit node (top node of a file) also has a premise tree node.
struct PremiseTree
{
    ProgramPoint programPoint;
    // For #if/#else bodies, there is only one premise needed for entering the body.
    z3::expr premise;
    // For macro expansions, for each different definition of the macro, we need a different premise,
    // Which means "what conditions you need for the macro to be expanded using this definition".
    // When using this map, use emplace or insert instead of operator[], because z3::expr is not default constructible.
    std::unordered_map<ProgramPoint, z3::expr, ProgramPoint::Hasher> macroPremises;

    std::vector<PremiseTreePtr> children;
    const PremiseTree * parent;

    static PremiseTreePtr make
    (
        const ProgramPoint & programPoint,
        const z3::expr & premise,
        const PremiseTree * parent = nullptr
    )
    {
        PremiseTreePtr tree = std::make_unique<PremiseTree>
        (
            programPoint,
            premise,
            parent
        );
        return tree;
    }

    PremiseTree * addChild
    (
        const ProgramPoint & programPoint,
        const z3::expr & premise
    )
    {
        PremiseTreePtr child = PremiseTree::make(programPoint, premise, this);
        children.push_back(std::move(child));
        return children.back().get();
    }

    // Users shall not use this constructor directly.
    // This it meant for std::make_shared<PremiseTree> to work.
    PremiseTree
    (
        const ProgramPoint & programPoint,
        const z3::expr & premise,
        const PremiseTree * parent
    )
        : programPoint(programPoint), premise(premise), parent(parent)
    {
    }

    bool isMacroExpansion() const
    {
        // If there are macro premises, this is a macro expansion.
        return !macroPremises.empty();
    }

    // Retrieve the complete premese, a conjunction of all premises of its ancestors.
    z3::expr getCompletePremise() const
    {
        z3::expr completePremise = premise;
        const PremiseTree * ancestor = parent;
        while (ancestor)
        {
            completePremise = completePremise && ancestor->premise;
            ancestor = ancestor->parent;
        }
        return completePremise;
    }

    void disjunctPremise(const z3::expr & premise)
    {
        SPDLOG_TRACE("Disjuncting premise: \n Program point: {}\n Premise: {}", programPoint.toString(), premise.to_string());
        this->premise = this->premise || premise;
        SPDLOG_TRACE("New premise: {}", this->premise.to_string());
    }

    void conjunctPremise(const z3::expr & premise)
    {
        SPDLOG_TRACE("Conjuncting premise: \n Program point: {}\n Premise: {}", programPoint.toString(), premise.to_string());
        this->premise = this->premise && premise;
        SPDLOG_TRACE("New premise: {}", this->premise.to_string());
    }

    void disjunctMacroPremise
    (
        const ProgramPoint & programPoint,
        const z3::expr & premise
    )
    {
        SPDLOG_TRACE("Disjuncting macro premise: \n Program point: {}\n Premise: {}", programPoint.toString(), premise.to_string());
        auto it = macroPremises.find(programPoint);
        if (it == macroPremises.end())
        {
            macroPremises.insert_or_assign(programPoint, premise);
        }
        else
        {
            it->second = it->second || premise;
        }
    }

    std::string toString(size_t depth = 0) const
    {
        std::string str(depth * 4, ' ');
        if (!isMacroExpansion())
        {
            str += std::format
            (
                "{} {}",
                programPoint.toString(),
                premise.to_string()
            );
        }
        else
        {
            str += std::format
            (
                "{} {}",
                programPoint.toString(),
                "Macro expansion:"
            );
            for (const auto & [programPoint, premise] : macroPremises)
            {
                str += std::format
                (
                    "\n{}{}: {}",
                    std::string((depth + 1) * 4, ' '),
                    programPoint.toString(),
                    premise.to_string()
                );
            }
        }
        for (const PremiseTreePtr & child : children)
        {
            str += "\n" + child->toString(depth + 1);
        }
        return str;
    }

    z3::model getModel() const
    {
        z3::expr complete = getCompletePremise();
        z3::solver s(complete.ctx());
        s.add(complete);
        z3::check_result r = s.check();
        if (r == z3::sat)
        {
            return s.get_model();
        }
        throw std::runtime_error("Cannot get model: premise is not satisfiable.");
    }

    DefineSet getDefineSet() const
    {
        return DefineSet(getModel());
    }

    // Simplify premises of all descendants.
    void refine()
    {
        premise = simplifyOrOfAnd(premise);
        std::unordered_map<ProgramPoint, z3::expr, ProgramPoint::Hasher> newMacroPremises;
        for (auto & [macroProgramPoint, macroPremise] : macroPremises)
        {
            if (z3CheckTautology(z3::implies(getCompletePremise(), macroPremise)))
            {
                SPDLOG_TRACE("Eliminating macro premise: {}", macroPremise.to_string());
                continue;
            }
            macroPremise = simplifyOrOfAnd(macroPremise);
            newMacroPremises.emplace(macroProgramPoint, macroPremise);
        }
        macroPremises = std::move(newMacroPremises);

        std::vector<PremiseTreePtr> newChildren;
        for (PremiseTreePtr & child : children)
        {
            child->refine();
            
            // If the child is always false, we can remove the child node.
            if (!child->isMacroExpansion() && z3CheckContradiction(child->getCompletePremise()))
            {
                SPDLOG_TRACE("Eliminating constant-false child node: {}", child->toString());
                continue;
            }

            // If the current node's premise implies the child's premise,
            // we can remove it and promote its children.
            if (!child->isMacroExpansion() && z3CheckTautology(z3::implies(getCompletePremise(), child->premise)))
            {
                SPDLOG_TRACE("Eliminating implied child node: {}", child->toString());
                for (PremiseTreePtr & grandchild : child->children)
                {
                    grandchild->parent = this;
                    newChildren.push_back(std::move(grandchild));
                }
            }
            else
            {
                newChildren.push_back(std::move(child));
            }
        }
        children = std::move(newChildren);
    }

    std::list<const PremiseTree *> getDescendantsPreOrder() const
    {
        std::list<const PremiseTree *> result;
        result.push_back(this);
        for (const PremiseTreePtr & child : children)
        {
            std::list<const PremiseTree *> childNodes = child->getDescendantsPreOrder();
            result.splice(result.end(), childNodes);
        }
        return result;
    }

    std::list<const PremiseTree *> getDescendantsLevelOrder() const
    {
        std::list<const PremiseTree *> order;
        std::queue<const PremiseTree *> q;
        q.push(this);
        while (!q.empty())
        {
            const PremiseTree * node = q.front();
            q.pop();
            order.push_back(node);
            for (const PremiseTreePtr & child : node->children)
            {
                q.push(child.get());
            }
        }
        return order;
    }

    // Find the smallest premise tree node that contains the target program point.
    const PremiseTree * findEnclosingNode(const ProgramPoint & target) const
    {
        assert(programPoint.contains(target));
        for (const PremiseTreePtr & child : children)
        {
            if (child->programPoint.contains(target))
            {
                return child->findEnclosingNode(target);
            }
        }
        return this;
    }

    // Generates code range analysis tasks for each descendant node.
    // The row and column number in the return value is that in the compilation unit file, i.e. line-mapped.
    std::vector<CodeRangeAnalysisTask> getCodeRangeAnalysisTasks
    (
        const std::unordered_map<Hayroll::IncludeTreePtr, std::vector<int>> & lineMap
    ) const
    {
        std::vector<CodeRangeAnalysisTask> tasks;
        for (const PremiseTree * premiseNode : getDescendantsPreOrder())
        {
            if (premiseNode->isMacroExpansion())
            {
                // We do not generate code range analysis tasks for macro expansions.
                continue;
            }
            const ProgramPoint & programPoint = premiseNode->programPoint;
            const IncludeTreePtr & includeTree = programPoint.includeTree;
            const TSNode & tsNode = programPoint.node;
            if (!lineMap.contains(includeTree))
            {
                SPDLOG_TRACE
                (
                    "IncludeTree {} not found in lineMap. Skipping premise {}.",
                    includeTree->stacktrace(),
                    premiseNode->premise.to_string()
                );
                continue; // Skip if the IncludeTree is not in the lineMap
            }
            const std::vector<int> & lineNumbers = lineMap.at(includeTree);
            CodeRangeAnalysisTask task =
            {
                .name = "PremiseTree-generated",
                .beginLine = lineNumbers.at(tsNode.startPoint().row + 1),
                .beginCol = static_cast<int>(tsNode.startPoint().column) + 1,
                .endLine = lineNumbers.at(tsNode.endPoint().row + 1),
                .endCol = static_cast<int>(tsNode.endPoint().column) + 1,
                .extraInfo = premiseNode->premise.to_string()
            };
            tasks.push_back(task);
        }
        return tasks;
    }
};

// A helper class that takes down info during symbolic execution to build the premise tree.
class PremiseTreeScribe
{
public:
    PremiseTreeScribe()
        : tree(nullptr), map(), init(false)
    {
    }

    PremiseTreeScribe(const ProgramPoint & programPoint, const z3::expr & premise)
        : tree(PremiseTree::make(programPoint, premise)), map(), init(true)
    {
        map.insert_or_assign(programPoint, tree.get());
    }

    void conjunctPremiseOntoRoot(const z3::expr & premise)
    {
        if (!init) return;
        assert(tree);
        tree->conjunctPremise(premise);
    }

    // Disjunct the premise with the existing one.
    void disjunctPremise(const ProgramPoint & programPoint, const z3::expr & premise)
    {
        if (!init) return;
        auto it = map.find(programPoint);
        assert(it != map.end());
        PremiseTree * treeNode = it->second;
        assert(treeNode);
        treeNode->disjunctPremise(premise);
    }

    // Create a new premise tree node and add the premise to it, automatically finding the parent node.
    PremiseTree * createNode(const ProgramPoint & programPoint, const z3::expr & premise)
    {
        if (!init) return nullptr;
        assert(!map.contains(programPoint));

        // Keep going to parent until such program point has a corresponding premise tree node.
        ProgramPoint ancestor = programPoint;
        PremiseTree * parent = nullptr;
        while (true)
        {
            if (auto it = map.find(ancestor); it != map.end())
            {
                parent = it->second;
                break;
            }
            // If we reach the root node, we can't find a parent.
            ancestor = ancestor.parent();
            assert(ancestor);
        }

        PremiseTree * newTree = parent->addChild(programPoint, premise);
        map.emplace(programPoint, newTree);

        SPDLOG_TRACE("Created new premise tree node: {}", newTree->toString());
        SPDLOG_TRACE("Parent premise tree node: {}", parent->toString());
        SPDLOG_TRACE("New premise: {}", newTree->premise.to_string());

        return newTree;
    }

    PremiseTreePtr takeTree()
    {
        PremiseTreePtr result = std::move(tree);
        map.clear();
        return result;
    }

    PremiseTree * borrowTree()
    {
        return tree.get();
    }

private:
    PremiseTreePtr tree;
    // A mapping from the program point to the premise tree node.
    // It does not need ownership so it's using raw pointers.
    std::unordered_map<ProgramPoint, PremiseTree *, ProgramPoint::Hasher> map;
    bool init;
};

} // namespace Hayroll

#endif // HAYROLL_PREMISETREE_HPP
