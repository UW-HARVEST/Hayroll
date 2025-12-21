#ifndef HAYROLL_SPLITTER_HPP
#define HAYROLL_SPLITTER_HPP

#include <format>
#include <list>
#include <optional>
#include <string>

#include <z3++.h>

#include <spdlog/spdlog.h>

#include "Util.hpp"
#include "DefineSet.hpp"
#include "PremiseTree.hpp"
#include "CompileCommand.hpp"

namespace Hayroll
{

class Splitter
{
public:
    enum FeedbackKind
    {
        Initial,
        Success,
        Fail
    };

    struct Feedback
    {
        FeedbackKind kind;
        std::string stage;
        std::string reason;

        static Feedback initial()
        {
            return Feedback{Initial, "", ""};
        }

        static Feedback success()
        {
            return Feedback{Success, "", ""};
        }

        static Feedback failStage(std::string_view stage, std::string_view reason)
        {
            return Feedback{Fail, std::string(stage), std::string(reason)};
        }
    };

    Splitter
    (
        const PremiseTree * premiseTree,
        const CompileCommand & compileCommand
    )
        : premiseTree(premiseTree), compileCommand(compileCommand)
    {
        if (!premiseTree) return;
        worklist = premiseTree->getDescendantsLevelOrder();
    }

    std::optional<DefineSet> next(const Feedback & feedback)
    {
        applyFeedback(feedback);

        if (!premiseTree) return std::nullopt;

        if (!worklist.empty())
        {
            const PremiseTree * node = worklist.back();
            worklist.pop_back();
            z3::expr premise = node->getCompletePremise();
            lastNode = node;
            lastDefineSet = node->getDefineSet();
            SPDLOG_TRACE
            (
                "Splitter generated DefineSet {} for {}",
                lastDefineSet->toString(),
                premise.to_string()
            );
            return lastDefineSet;
        }

        reportUncovered();
        return std::nullopt;
    }

private:
    void applyFeedback(const Feedback & feedback)
    {
        if (feedback.kind == Initial)
        {
            return;
        }
        else if (feedback.kind == Success)
        {
            removeSatisfiedNodes(*lastDefineSet);
        }
        else if (feedback.kind == Fail)
        {
            std::string stageStr = feedback.stage.empty() ? "" : std::format(" at stage {}", feedback.stage);
            std::string reasonStr = feedback.reason.empty() ? "" : std::format(" ({})", feedback.reason);
            SPDLOG_TRACE
            (
                "Splitter treating DefineSet {} as failed{}{}.",
                lastDefineSet->toString(),
                stageStr,
                reasonStr
            );
            uncovered.push_back(lastNode);
        }

        lastDefineSet.reset();
        lastNode = nullptr;
    }

    void removeSatisfiedNodes(const DefineSet & defineSet)
    {
        for (auto it = worklist.begin(); it != worklist.end(); )
        {
            const PremiseTree * otherNode = *it;
            z3::expr otherPremise = otherNode->getCompletePremise();
            if (defineSet.satisfies(otherPremise))
            {
                SPDLOG_TRACE
                (
                    "DefineSet {} satisfies premise tree node {}, removing it from worklist.",
                    defineSet.toString(),
                    otherNode->toString()
                );
                it = worklist.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    void reportUncovered() const
    {
        if (reportedUncovered || uncovered.empty()) return;

        SPDLOG_DEBUG("Splitter reached end of worklist for {}.", compileCommand.file.string());
        SPDLOG_DEBUG("The following premise tree nodes could not be covered by any successful DefineSet:");
        for (const PremiseTree * node : uncovered)
        {
            SPDLOG_DEBUG(" - Node: {}", node->toString());
            SPDLOG_DEBUG("   Premise: {}", node->getCompletePremise().to_string());
        }
        reportedUncovered = true;
    }

    const PremiseTree * premiseTree;
    CompileCommand compileCommand;
    std::list<const PremiseTree *> worklist;
    std::list<const PremiseTree *> uncovered;
    std::optional<DefineSet> lastDefineSet;
    const PremiseTree * lastNode{nullptr};
    mutable bool reportedUncovered{false};
};

} // namespace Hayroll

#endif // HAYROLL_SPLITTER_HPP
