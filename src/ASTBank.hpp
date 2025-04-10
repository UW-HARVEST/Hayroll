// A data structure that owns ASTs parsed from sources
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

namespace Hayroll
{

class ASTBank
{
public:
    ASTBank(const ts::TSLanguage * language)
        : parser(language)
    {
    }

    // Add a file to the bank. The bank parses the file and stores the syntax tree.
    const TSTree & addFile(const std::filesystem::path & path)
    {
        std::ifstream file(path);
        if (!file.is_open())
        {
            throw std::runtime_error("Failed to open file: " + path.string());
        }
        std::string fullSrc = std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        file.close();
        
        TSTree tree = parser.parseString(std::move(fullSrc));
        bank[path] = std::move(tree);

        return bank.at(path);
    }

    const TSTree & addAnonymousSource(std::string && src)
    {
        TSTree tree = parser.parseString(std::move(src));
        anonymousSources.push_back(std::move(tree));
        return anonymousSources.back();
    }

    // Find a tree in the bank by file path
    const TSTree & find(const std::filesystem::path & path) const
    {
        return bank.at(path);
    }

private:
    TSParser parser;
    std::unordered_map<std::filesystem::path, TSTree> bank;
    std::vector<TSTree> anonymousSources;
};

} // namespace Hayroll

#endif // HAYROLL_ASTBANK_HPP
