#include <iostream>
#include <filesystem>
#include <fstream>

#include <spdlog/spdlog.h>

#include "subprocess.hpp"

#include "TempDir.hpp"
#include "IncludeResolver.hpp"
#include "ASTBank.hpp"
#include "MacroExpander.hpp"

int main(int argc, char **argv)
{
    using namespace Hayroll;

    spdlog::set_level(spdlog::level::debug);

    SymbolTablePtr symbolTable = SymbolTable::make();

    auto tmpDir = TempDir();
    auto tmpPath = tmpDir.getPath();

    auto srcPath = tmpPath / "test.c";
    std::ofstream srcFile(srcPath);
    // Use raw string literals to avoid escaping
    srcFile << 
    R"(
        #define A 1
        #if A
        #endif
        #if defined(A)
        #endif
        #undef A
        #if A
        #endif
        #if defined(A)
        #endif
        #if B
        #endif
        #define A defined A
        #if A
        #endif
        #define F(x) ((x) + 1)
        #if F(1)
        #endif
        #if F(F(F(1)))
        #endif
        #if F(B) + 1
        #endif
        #define E
        #if E
        #endif
    )";
    srcFile.close();
    
    CPreproc lang = CPreproc();

    ASTBank astBank(lang);
    astBank.addFile(srcPath);

    MacroExpander expander(lang);

    const TSTree & tree = astBank.find(srcPath);
    TSNode root = tree.rootNode();
    
    // There are only preproc_def, preproc_function_def, preproc_undef and preproc_if nodes
    // Seeing a def or undef, add to the symbol table
    // Seeing an if, expand the condition and print it
    for (TSNode node : root.iterateChildren())
    {
        assert(node.isSymbol(lang.preproc_def_s) || node.isSymbol(lang.preproc_function_def_s) || node.isSymbol(lang.preproc_undef_s) || node.isSymbol(lang.preproc_if_s));
        
        if (node.isSymbol(lang.preproc_def_s))
        {
            TSNode name = node.childByFieldId(lang.preproc_def_s.name_f);
            TSNode value = node.childByFieldId(lang.preproc_def_s.value_f); // May not exist
            std::string nameStr = name.text();
            std::string valueStr = value ? value.text() : "";
            symbolTable->define(ObjectSymbol{std::move(nameStr), std::move(valueStr)});
            std::cout << node.text();
        }
        else if (node.isSymbol(lang.preproc_function_def_s))
        {
            TSNode name = node.childByFieldId(lang.preproc_function_def_s.name_f);
            TSNode params = node.childByFieldId(lang.preproc_function_def_s.parameters_f);
            assert(params.isSymbol(lang.preproc_params_s));
            TSNode body = node.childByFieldId(lang.preproc_function_def_s.value_f); // May not exist
            std::string nameStr = name.text();
            std::vector<std::string> paramsStrs;
            for (TSNode param : params.iterateChildren())
            {
                if (!param.isSymbol(lang.identifier_s)) continue;
                paramsStrs.push_back(param.text());
            }
            std::string bodyStr = body ? body.text() : "";
            symbolTable->define(FunctionSymbol{std::move(nameStr), std::move(paramsStrs), std::move(bodyStr)});
            std::cout << node.text();
        }
        else if (node.isSymbol(lang.preproc_undef_s))
        {
            TSNode name = node.childByFieldId(lang.preproc_undef_s.name_f);
            std::string nameStr = name.text();
            symbolTable->define(UndefinedSymbol{nameStr});
            std::cout << node.text();
        }
        else if (node.isSymbol(lang.preproc_if_s))
        {
            TSNode condition = node.childByFieldId(lang.preproc_if_s.condition_f);
            assert(condition.isSymbol(lang.preproc_tokens_s));
            std::string expanded = expander.expandPreprocTokens(condition, symbolTable);
            std::cout << condition.text() << " -> " << expanded << std::endl;
        }
        else assert(false);
    }

    return 0;
}
