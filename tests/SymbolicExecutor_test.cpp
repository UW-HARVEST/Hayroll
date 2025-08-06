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

    // Disable let bindings in printing
    z3::set_param("pp.min_alias_size", 1000000);
    z3::set_param("pp.max_depth", 1000000);

    TempDir tmpDir(false);
    std::filesystem::path tmpPath = tmpDir.getPath();

    auto saveSource = [&tmpPath](const std::string & source, const std::string & filename) -> std::filesystem::path
    {
        std::filesystem::path srcPath = tmpPath / filename;
        std::ofstream srcFile(srcPath);
        srcFile << source;
        srcFile.close();
        return srcPath;
    };

    std::string testSrcString = 
    R"(
        #ifdef INCLUDE_IMPOSSIBLE
            #check 0
            #include "macrohard.h"
        #endif

        #ifdef __UINT32_MAX__
            #check !defined INCLUDE_IMPOSSIBLE && !defined USER_E
        #else
            #check 0
        #endif

        #include "inc.h" // #undef CODE_F

        #undef __WORDSIZE
        #if __WORDSIZE == 0
            #check !defined INCLUDE_IMPOSSIBLE && !defined USER_E
        #endif
        #include <math.h>
        #if __WORDSIZE >= 32
            #check !defined INCLUDE_IMPOSSIBLE && !defined USER_E
        #endif

        #ifdef USER_C
            #define __USER_C 1
        #endif

        #ifndef USER_A
            #check !defined INCLUDE_IMPOSSIBLE && !defined USER_E && !defined USER_A
            #if USER_D > USER_A // USER_D > 0
                #define SOMETHING() THAT_BLOCKS_STATE_MERGING
                #check !defined INCLUDE_IMPOSSIBLE && !defined USER_E && !defined USER_A && USER_D > 0
            #endif
        #elifndef USER_B
            #check !defined INCLUDE_IMPOSSIBLE && !defined USER_E && defined USER_A && !defined USER_B
            #define SOMETHING() ALTERNATIVELY_THAT_DEPENDS_ON __USER_C
        #elifdef CODE_F
            #check 0
        #elifdef USER_C
            #check !defined INCLUDE_IMPOSSIBLE && !defined USER_E && defined USER_A && defined USER_B && defined USER_C
        #elif defined USER_A && defined USER_B && !defined USER_C
            #check !defined INCLUDE_IMPOSSIBLE && !defined USER_E && defined USER_A && defined USER_B && !defined USER_C
        #else
            #check 0
        #endif

        #if 1
            #check !defined INCLUDE_IMPOSSIBLE && !defined USER_E
            // Even though states splitted, every thread will reach this point
            #define CODE_F
        #endif

        #ifdef USER_E
            #check 0
            #error
        #endif

        SOMETHING() // There should be a warning about its different expansion w.r.t. USER_C

    )";
    std::filesystem::path entryPath = saveSource(testSrcString, "test.c");

    std::string incSrcString =
    R"(
        #check !defined INCLUDE_IMPOSSIBLE && !defined USER_E
        #undef CODE_F
    )";
    saveSource(incSrcString, "inc.h");

    std::vector<SymbolicExecutor> executors;

    executors.push_back(std::move(SymbolicExecutor(entryPath, tmpPath)));

    executors.push_back(std::move(SymbolicExecutor(LibmcsDir / "libm/mathf/sinhf.c", LibmcsDir, {LibmcsDir / "libm/include/"})));
    executors.push_back(std::move(SymbolicExecutor(LibmcsDir / "libm/complexd/cabsd.c", LibmcsDir, {LibmcsDir / "libm/include/"})));
    
    bool allPass = true;

    for (SymbolicExecutor & executor : executors)
    {

        Warp endWarp = executor.run();
        PremiseTree * premiseTree = executor.scribe.borrowTree();
        IncludeTreePtr includeTree = executor.includeTree;
        const CPreproc & lang = executor.lang;

        for (const State & state : endWarp.states)
        {
            std::cout << std::format("End state:\n{}\n==============\n", state.toStringFull());
        }

        std::cout << "Premise tree:\n";
        std::cout << premiseTree->toString() << std::endl;

        premiseTree->refine();
        std::cout << "Refined premise tree:\n";
        std::cout << premiseTree->toString() << std::endl;

        std::cout << "Include tree:\n";
        std::cout << includeTree->toString() << std::endl;

        for (const PremiseTree * node : premiseTree->getDescendants())
        {
            if (node->premise.to_string().size() > 1024)
            {
                std::cout << "Warning: inconcise premise. \n";
                allPass = false;
            }
        }

        // Checking: every premise carried by a #check line should imply the premise of the smallest premise tree node it is in.
        for (const IncludeTreePtr includeTreeNode : *includeTree)
        {
            if (includeTreeNode->isSystemInclude) continue;
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
                    writtenPremise = simplifyOrOfAnd(writtenPremise);
                    z3::expr premise = premiseTreeNode->getCompletePremise();
                    z3::expr premiseEqWrittenPremise = premise == writtenPremise;
                    z3::solver s(*executor.ctx);
                    s.add(!premiseEqWrittenPremise);
                    std::cout << std::format("Checking written premise at {}\n", programPoint.toString());
                    if (s.check() == z3::unsat)
                    {
                        std::cout << std::format("Pass: Premise {} is equivalent to written premise {}\n", premise.to_string(), writtenPremise.to_string());
                    }
                    else
                    {
                        std::cout << std::format("Error: Premise {} is not equivalent to written premise {}\n", premise.to_string(), writtenPremise.to_string());
                        allPass = false;
                    }
                }
            }
        }
    }

    if (!allPass) return 1;

    std::cout << "All checks passed.\n";
    return 0;
}
