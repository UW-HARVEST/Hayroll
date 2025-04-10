#include <iostream>
#include <filesystem>
#include <fstream>
#include <format>

#include <z3++.h>
#include <spdlog/spdlog.h>

#include "subprocess.hpp"

#include "TempDir.hpp"
#include "IncludeResolver.hpp"
#include "IncludeTree.hpp"
#include "ASTBank.hpp"
#include "MacroExpander.hpp"

int main(int argc, char **argv)
{
    using namespace Hayroll;

    spdlog::set_level(spdlog::level::debug);

    IncludeTreePtr includeTree = IncludeTree::make(0, "<PREDEFINED_MACROS>");
    SymbolTablePtr symbolTable = SymbolTable::make();

    TempDir tmpDir;
    std::filesystem::path tmpPath = tmpDir.getPath();

    // List[(List[(defOrUndef)], List[(bench, Option[expected])]]
    // Having no expected value means error is expected
    std::vector<std::tuple
    <
        std::vector<std::string>,
        std::vector<std::pair<std::string, std::optional<std::string>>>
    >> testbenches =
    {
        {
            {},
            {
                { "A", "A" },
                { "defined(A)", "defined ( A )" },
                { "!A", "! A" },
                { "!defined(A)", "! defined ( A )" },
            },
        },
        {
            {
                "#define A"
            },
            {
                { "A", "" },
                { "defined(A)", "1" },
                { "!A", "!" },
                { "!defined(A)", "! 1" },
            },
        },
        {
            {
                "#define A 1"
            },
            {
                { "A", "1" },
                { "defined(A)", "1" },
                { "!A", "! 1" },
                { "!defined(A)", "! 1" },
            },
        },
        {
            {
                "#undef A"
            },
            {
                { "A", "0" },
                { "defined(A)", "0" },
                { "!A", "! 0" },
                { "!defined(A)", "! 0" },
            },
        },
        {
            {
                { "#define A defined A" },
            },
            {
                { "A", "1" },
                { "defined(A)", "1" },
                { "!A", "! 1" },
                { "!defined(A)", "! 1" },
            },
        },
        {
            {
                "#define A 3",
                "#define F(x) ((x) + 1)",
            },
            {
                { "F(1)", "( ( 1 ) + 1 )" },
                { "F(F(A))", "( ( ( ( 3 ) + 1 ) ) + 1 )" },
                { "F(F(F(2)))", "( ( ( ( ( ( 2 ) + 1 ) ) + 1 ) ) + 1 )" },
                { "F(B) + 1", "( ( B ) + 1 ) + 1" },
                { "F((x ? x : x))", "( ( ( x ? x : x ) ) + 1 )" },
            },
        },
        {
            {
                "#define F(x) x",
                "#define Y 1 + F",
            },
            {
                { "Y(2)", "1 + 2" },
            },
        },
        {
            {
                "#define F(x) (x + x)",
                "#define Y F(",
            },
            {
                { "Y 1)", "( 1 + 1 )" },
                { "Y 1) | Y 2)", "( 1 + 1 ) | ( 2 + 2 )" },
            },
        },
        {
            {
                "#define F(x) (x + x)",
                "#define Y F(",
                "#define Z F",
            },
            {
                { "T Y Z (1))", "T ( ( 1 + 1 ) + ( 1 + 1 ) )" },
            },
        },
        {
            {
                "#define G(a, b) a + b",
                "#define F(x) x(1",
            },
            {
                { "F(G), 2)", "1 + 2" },
            },
        },
        {
            {
                "#define D defined",
                "#define A D A",
            },
            {
                { "A", "1" },
                { "defined(A)", "1" },
                { "!A", "! 1" },
                { "!defined(A)", "! 1" },
            },
        },
        {
            {
                "#define A A A",
                "#define F(x) x",
                "#define G(x) A",
                "#define H(x) B",
            },
            {
                { "A", std::nullopt },
                { "F(A)", std::nullopt },
                { "G(1)", std::nullopt },
                { "H(A)", std::nullopt }, // Expected false-positive due to implementation limitations
            },
        },
    };

    // i-th #if -> expected
    std::vector<std::optional<std::string_view>> testbenchesRef;

    std::string srcString;
    for (const auto & [defs, benches] : testbenches)
    {
        for (const auto & def : defs)
        {
            srcString += def + "\n";
        }
        for (const auto & [bench, expected] : benches)
        {
            srcString += "#if ";
            srcString += bench + "\n";
            srcString += "#endif\n";
            testbenchesRef.push_back(expected);
        }
    }

    std::filesystem::path srcPath = tmpPath / "test.c";
    std::ofstream srcFile(srcPath);
    // Use raw string literals to avoid escaping
    srcFile << srcString;
    srcFile.close();
    
    CPreproc lang = CPreproc();

    ASTBank astBank(lang);
    astBank.addFile(srcPath);

    z3::context ctx;

    MacroExpander expander(lang, ctx);

    const TSTree & tree = astBank.find(srcPath);
    TSNode root = tree.rootNode();

    size_t trialId = 0;
    bool allPassed = true;
    bool lastWasIf = true;
    
    // There are only preproc_def, preproc_function_def, preproc_undef and preproc_if nodes
    // Seeing a def or undef, add to the symbol table
    // Seeing an if, expand the condition and print it
    for (TSNode node : root.iterateChildren())
    {
        assert(node.isSymbol(lang.preproc_def_s) || node.isSymbol(lang.preproc_function_def_s) || node.isSymbol(lang.preproc_undef_s) || node.isSymbol(lang.preproc_if_s));
        
        if (node.isSymbol(lang.preproc_def_s) || node.isSymbol(lang.preproc_function_def_s) || node.isSymbol(lang.preproc_undef_s))
        {
            // Clear the symbol table if the last node was an if
            if (lastWasIf)
            {
                symbolTable = SymbolTable::make();
                lastWasIf = false;
            }
        }

        if (node.isSymbol(lang.preproc_def_s))
        {
            TSNode name = node.childByFieldId(lang.preproc_def_s.name_f);
            TSNode value = node.childByFieldId(lang.preproc_def_s.value_f); // May not exist
            std::string_view nameStr = name.textView();
            symbolTable = symbolTable->define(ObjectSymbol{nameStr, {includeTree, node}, value});
            std::cout << node.text();
        }
        else if (node.isSymbol(lang.preproc_function_def_s))
        {
            TSNode name = node.childByFieldId(lang.preproc_function_def_s.name_f);
            TSNode params = node.childByFieldId(lang.preproc_function_def_s.parameters_f);
            assert(params.isSymbol(lang.preproc_params_s));
            TSNode body = node.childByFieldId(lang.preproc_function_def_s.value_f); // May not exist
            std::string_view nameStr = name.textView();
            std::vector<std::string> paramsStrs;
            for (TSNode param : params.iterateChildren())
            {
                if (!param.isSymbol(lang.identifier_s)) continue;
                paramsStrs.push_back(param.text());
            }
            symbolTable = symbolTable->define(FunctionSymbol{nameStr, {includeTree, node}, std::move(paramsStrs), body});
            std::cout << node.text();
        }
        else if (node.isSymbol(lang.preproc_undef_s))
        {
            TSNode name = node.childByFieldId(lang.preproc_undef_s.name_f);
            std::string_view nameStr = name.textView();
            symbolTable = symbolTable->define(UndefinedSymbol{nameStr});
            std::cout << node.text();
        }
        else if (node.isSymbol(lang.preproc_if_s))
        {
            TSNode condition = node.childByFieldId(lang.preproc_if_s.condition_f);
            assert(condition.isSymbol(lang.preproc_tokens_s));
            std::vector<TSNode> conditionTokens = lang.tokensToTokenVector(condition);

            bool isError = false;
            std::vector<Hayroll::TSNode> expanded;
            try
            {
                expanded = expander.expandPreprocTokens(conditionTokens, symbolTable);
            }
            catch (const std::runtime_error & e)
            {
                isError = true;
            }
            
            std::string expandedStr;
            if (isError)
            {
                expandedStr = "ERROR";
            }
            else
            {
                for (const TSNode & token : expanded)
                {
                    expandedStr += token.text() + " ";
                }
                // Remove trailing space
                if (!expandedStr.empty())
                {
                    expandedStr.pop_back();
                }
            }
            bool pass = (!testbenchesRef[trialId] && isError) || (testbenchesRef[trialId] && expandedStr == testbenchesRef[trialId].value());
            std::cout << std::format("{} expanded: {} -> {}\n", pass ? "OK" : "FAIL", condition.text(), expandedStr);

            // Try symbolizing if the expansion was successful
            if (pass && !isError)
            {
                try
                {
                    auto [exprTree, exprNode] = expander.parseIntoExpression(expandedStr);
                    if (!exprNode)
                    {
                        std::cout << "Not symbolizing an empty expression" << std::endl;
                    }
                    else
                    {
                        z3::expr expr = expander.int2bool(expander.symbolizeExpression(exprNode));
                        expr = z3CtxtSolverSimplify(expr);
                        std::cout << std::format("Symbolized: \n{}\n", expr.to_string()) << std::endl;
                    }
                }
                catch (const std::runtime_error & e)
                {
                    std::cout << "Symbolization failed: " << e.what() << std::endl;
                }
            }

            ++trialId;
            if (!pass)
            {
                allPassed = false;
            }

            lastWasIf = true;
        }
        else assert(false);
    }

    if (!allPassed)
    {
        std::cerr << "Some expansions failed\n";
    }
    else
    {
        std::cout << "All  expansions passed\n";
    }

    return allPassed ? 0 : 1;
}
