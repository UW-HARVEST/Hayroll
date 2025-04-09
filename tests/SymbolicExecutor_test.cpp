#include <iostream>

#include <spdlog/spdlog.h>

#include "SymbolicExecutor.hpp"
#include "PremiseTree.hpp"

int main(int argc, char **argv)
{
    using namespace Hayroll;

    spdlog::set_level(spdlog::level::debug);

    TempDir tmpDir;
    std::filesystem::path tmpPath = tmpDir.getPath();

    std::string srcString = 
    R"(
        #if A
        #endif
        #if defined(A)
        #endif
        #if !A
        #endif
        #if !defined(A)
        #endif
        #define A
        #if A
        #endif
        #if defined(A)
        #endif
        #if !A
        #endif
        #if !defined(A)
        #endif
        #define A 1
        #if A
        #endif
        #if defined(A)
        #endif
        #if !A
        #endif
        #if !defined(A)
        #endif
        #undef A
        #if A
        #endif
        #if defined(A)
        #endif
        #if !A
        #endif
        #if !defined(A)
        #endif
        #define A defined A
        #if A
        #endif
        #if defined(A)
        #endif
        #if !A
        #endif
        #if !defined(A)
        #endif
        #define A 3
        #define F(x) ((x) + 1)
        #if F(1)
        #endif
        #if F(F(A))
        #endif
        #if F(F(F(2)))
        #endif
        #if F(B) + 1
        #endif
        #if F((x ? x : x))
        #endif
        #define F(x) x
        #define Y 1 + F
        #if Y(2)
        #endif
        #define F(x) (x + x)
        #define Y F(
        #if Y 1)
        #endif
        #if Y 1) | Y 2)
        #endif
        #define F(x) (x + x)
        #define Y F(
        #define Z F
        #if T Y Z (1))
        #endif
        #define G(a, b) a + b
        #define F(x) x(1
        #if F(G), 2)
        #endif
        #define D defined
        #define A D A
        #if A
        #endif
        #if defined(A)
        #endif
        #if !A
        #endif
        #if !defined(A)
        #endif
        #define A A A
        #define F(x) x
        #define G(x) A
        #define H(x) B
        #if A
        #endif
        #if F(A)
        #endif
        #if G(1)
        #endif
        #if H(A)
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
        std::cout << std::format("End state:\n{}\n", state.toString());
    }

    return 0;
}
