#include <iostream>

#include <z3++.h>

#include <spdlog/spdlog.h>

#include "Util.hpp"
#include "SymbolicExecutor.hpp"
#include "PremiseTree.hpp"

int main(int argc, char **argv)
{
    using namespace Hayroll;

    spdlog::set_level(spdlog::level::debug);

    TempDir tmpDir(false);
    std::filesystem::path tmpPath = tmpDir.getPath();

    std::string srcString = 
    R"(
        #ifdef __UINT32_MAX__
            #check !defined USER_E
        #else
            #check 0
        #endif

        #ifndef USER_A
            #check !defined USER_E && !defined USER_A
            #if USER_D > USER_A // USER_D > 0
                #define SOMETHING THAT_BLOCKS_STATE_MERGING
                #check !defined USER_E && !defined USER_A && USER_D > 0
            #endif
        #elifndef USER_B
            #check !defined USER_E && defined USER_A && !defined USER_B
        #elifdef USER_C
            #check !defined USER_E && defined USER_A && defined USER_B && defined USER_C
        #elif defined USER_A && defined USER_B && !defined USER_C
            #check !defined USER_E && defined USER_A && defined USER_B && !defined USER_C
        #else
            #check 0
        #endif

        #if 1
            #check !defined USER_E
            // Even though states splitted, every thread will reach this point
        #endif

        #ifdef USER_E
            #check 0
            #error
        #endif

        #if 2
            #check !defined USER_E
        #endif
    )";

    std::filesystem::path srcPath = tmpPath / "test.c";
    std::ofstream srcFile(srcPath);
    // Use raw string literals to avoid escaping
    srcFile << srcString;
    srcFile.close();

    SymbolicExecutor executor(srcPath);
    std::vector<State> endStates = executor.run();
    PremiseTree * premiseTree = executor.scribe.borrowTree();
    ConstIncludeTreePtr includeTree = executor.includeTree;
    const CPreproc & lang = executor.lang;

    for (const State & state : endStates)
    {
        std::cout << std::format("End state:\n{}\n==============\n", state.toStringFull());
    }

    std::cout << "Premise tree:\n";
    std::cout << premiseTree->toString() << std::endl;

    premiseTree->refine();
    std::cout << "Refined premise tree:\n";
    std::cout << premiseTree->toString() << std::endl;

    // Checking: every premise carried by a #check line should imply the premise of the smallest premise tree node it is in.
    for (const ConstIncludeTreePtr includeTreeNode : *includeTree)
    {
        TSNode astRoot = executor.astBank.find(includeTreeNode->path).rootNode();
        for (const TSNode & node : astRoot.iterateDescendants())
        {
            if (node && node.isSymbol(lang.preproc_call_s) && node.childByFieldId(lang.preproc_call_s.directive_f).textView() == "#check")
            {
                ProgramPoint programPoint{includeTreeNode, node};
                const PremiseTree * premiseTreeNode = premiseTree->findEnclosingNode(programPoint);
                TSNode writtenPremiseNode = node.childByFieldId(lang.preproc_call_s.argument_f);
                assert(writtenPremiseNode.isSymbol(lang.preproc_tokens_s));
                std::vector<TSNode> writtenPremiseTokens = lang.tokensToTokenVector(writtenPremiseNode);
                z3::expr writtenPremise = executor.macroExpander.symbolizeToBoolExpr(std::move(writtenPremiseTokens));
                writtenPremise = combinedSimplify(writtenPremise);
                z3::expr premise = premiseTreeNode->premise;
                z3::expr writtenPremiseImpliesPremise = z3::implies(writtenPremise, premise);
                z3::solver s(executor.ctx);
                s.add(!writtenPremiseImpliesPremise);
                std::cout << std::format("Checking written premise at {}\n", programPoint.toString());
                if (s.check() == z3::unsat)
                {
                    std::cout << std::format("Written premise {} implies premise {}\n", writtenPremise.to_string(), premise.to_string());
                }
                else
                {
                    std::cout << std::format("Written premise {} does not imply premise {}\n", writtenPremise.to_string(), premise.to_string());
                    return 1;
                }
            }
        }
    }

    return 0;
}
