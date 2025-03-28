// A data structure that holds a syntax tree and its source code
// Supports finding trees by file path

#ifndef HAYROLL_ASTBANK_HPP
#define HAYROLL_ASTBANK_HPP

#include <unordered_map>
#include <fstream>
#include <filesystem>
#include <tuple>

#include "subprocess.hpp"

#include "TreeSitter.hpp"
#include "TempDir.hpp"

#ifndef HAYROLL_MACRO_SKELETON_EXE
    #error "HAYROLL_MACRO_SKELETON_EXE must be defined"
#endif

namespace Hayroll
{

class ASTBank
{
public:
    ASTBank(const ts::TSLanguage * language)
        : parser(language)
    {
    }

    void addFile(const std::filesystem::path & path)
    {
        // Read file into string
        std::ifstream file(path);
        if (!file.is_open())
        {
            throw std::runtime_error("Failed to open file: " + path.string());
        }
        std::string fullSrc = std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        file.close();

        // Create a temporary directory and write the full source to a file
        TempDir tmpDir;
        auto tmpPath = tmpDir.getPath();
        auto tmpSrcPath = tmpPath / "src.c";
        std::ofstream tmpSrcFile(tmpSrcPath);
        tmpSrcFile << fullSrc;
        tmpSrcFile.close();

        // Parse the macro skeleton from the source
        std::vector<std::string> ccArgs = {HAYROLL_MACRO_SKELETON_EXE, tmpSrcPath};
        subprocess::Popen proc
        (
            ccArgs,
            subprocess::output{subprocess::PIPE},
            subprocess::error{subprocess::PIPE}
        );
        auto [out, err] = proc.communicate();
        std::string macroSkeleton = out.buf.data();
        
        // Parse the macro skeleton into a tree
        TSTree tree = parser.parseString(std::move(macroSkeleton));

        // Store the tree in the bank
        bank[path] = std::move(tree);
    }

    const TSTree & find(const std::filesystem::path & path) const
    {
        return bank.at(path);
    }

private:
    TSParser parser;
    std::unordered_map<std::filesystem::path, TSTree> bank;
};

} // namespace Hayroll

#endif // HAYROLL_ASTBANK_HPP
