#ifndef HAYROLL_REWRITEINCLUDESWRAPPER_HPP
#define HAYROLL_REWRITEINCLUDESWRAPPER_HPP

#include <string>
#include <filesystem>

#include <spdlog/spdlog.h>
#include "subprocess.hpp"
#include "json.hpp"

#include "Util.hpp"
#include "TempDir.hpp"
#include "CompileCommand.hpp"

namespace Hayroll
{

// A wrapper around the -frewrite-includes flag of clang.
// Takes a compilation command and outputs a string with the rewritten includes.
class RewriteIncludesWrapper
{
public:
    static std::string runRewriteIncludes(const CompileCommand & compileCommand)
    {
        TempDir tempDir;
        std::filesystem::path outputPath = tempDir.getPath() / "rewrite_includes.cu.c";
        std::filesystem::path sourcePath = compileCommand.file;

        std::vector<std::string> clangArgs =
        {
            ClangExe.string(),
            "-E",
            "-frewrite-includes"
        };

        for (const auto& arg : compileCommand.arguments)
        {
            if (arg.starts_with("-D") || arg.starts_with("-I"))
            {
                clangArgs.push_back(arg);
            }
        }

        clangArgs.push_back("-o");
        clangArgs.push_back(outputPath.string());
        clangArgs.push_back(sourcePath.string());

        std::string clangArgsStr;
        for (const auto& arg : clangArgs)
        {
            clangArgsStr += arg + " ";
        }
        SPDLOG_TRACE
        (
            "cwd to {} and issuing command: {}",
            compileCommand.directory.string(),
            clangArgsStr
        );

        subprocess::Popen clangProcess
        (
            clangArgs,
            subprocess::cwd{compileCommand.directory.string().c_str()},
            subprocess::output{subprocess::PIPE},
            subprocess::error{subprocess::PIPE}
        );

        clangProcess.communicate();

        std::string cuStr = loadFileToString(outputPath);
        return cuStr;
    }
};

} // namespace Hayroll

#endif // HAYROLL_REWRITEINCLUDESWRAPPER_HPP
