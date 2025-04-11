#include <iostream>

#include <spdlog/spdlog.h>

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
            MUST_REACH
        #else
            MUST_NOT_REACH
        #endif

        #ifndef USER_A
            MAY_REACH(true, !defUSER_A)
            
            #if USER_D > USER_A // USER_D > 0
            #define SOMETHING THAT_BLOCKS_STATE_MERGING
            MAY_REACH(defUSER_A, valUSER_D > 0)
            #endif

        #elifndef USER_B
            MAY_REACH(defUSER_A, !defUSER_B)
        #elifdef USER_C
            MAY_REACH(defUSER_A && defUSER_B, defUSER_C)
        #elif defined USER_A && defined USER_B && !defined USER_C
            MAY_REACH(defUSER_A && defUSER_B && !defUSER_C, true)
        #else
            MUST_NOT_REACH
        #endif
    )";

    std::filesystem::path srcPath = tmpPath / "test.c";
    std::ofstream srcFile(srcPath);
    // Use raw string literals to avoid escaping
    srcFile << srcString;
    srcFile.close();

    SymbolicExecutor executor(srcPath);
    std::vector<State> endStates = executor.run();

    for (const State & state : endStates)
    {
        std::cout << std::format("End state:\n{}\n==============\n", state.toStringFull());
    }

    std::cout << executor.scribe.borrowTree()->toString() << std::endl;

    return 0;
}
