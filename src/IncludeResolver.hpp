// Resolves include paths using the given C compiler
// Resolving means mapping the include name (e.g. stdio.h) to the actual file path (e.g. /usr/include/stdio.h)

#ifndef HAYROLL_INCLUDERESOLVER_HPP
#define HAYROLL_INCLUDERESOLVER_HPP

#include <string>
#include <filesystem>
#include <vector>
#include <fstream>
#include <stdio.h>

#include <spdlog/spdlog.h>

#include "TempDir.hpp"
#include "subprocess.hpp"

namespace Hayroll
{

class IncludeResolver
{
public:
    IncludeResolver(const std::string & ccExePath, const std::vector<std::filesystem::path> & includePaths)
        : ccExePath(ccExePath)
    {
        for (const auto & includePath : includePaths)
        {
            this->includePaths.push_back(std::filesystem::canonical(includePath));
        }
    }

    // Resolve an include path using the given C compiler
    // Parent paths are also necessary for user includes
    // You can generate that with InludeTree::getAncestorDirs()
    std::filesystem::path resolveInclude
    (
        bool isSystemInclude,
        std::string_view includeName,
        const std::vector<std::filesystem::path> & parentPaths // Accepted in leave-first order
    ) const
    {
        // Create a stub file with the include directive
        // Containing only #include <includeName> or #include "includeName"
        // Then cc -H it and parse the output with parseStubIncludePath
        // If is a system include, do not search in parent paths
        // cc -H -fsyntax-only -I{includePaths} stub.c
        // If is a user include, prioritize parent paths
        // cc -H -fsyntax-only -I{parentPaths} -I{includePaths} stub.c

        if (includeName.starts_with("<")) return includeName; // <built-in> or <command-line>
        // Short circuit if the include name is absolute
        // This saves time especially for LineMatcher, which has many system includes shown in absolute paths
        if (std::filesystem::path(includeName).is_absolute()) return std::filesystem::canonical(includeName);

        TempDir tmpDir;
        std::filesystem::path stubPath = tmpDir.getPath() / "stub.c";
        std::ofstream stubFile(stubPath);
        if (isSystemInclude)
        {
            stubFile << "#include <" << includeName << ">\n";
        }
        else
        {
            stubFile << "#include \"" << includeName << "\"\n";
        }
        stubFile.close();

        std::vector<std::string> ccArgs = {ccExePath, "-H", "-fsyntax-only", stubPath};
        if (!isSystemInclude)
        {
            for (const std::filesystem::path & parentPath : parentPaths)
            {
                ccArgs.push_back("-I" + parentPath.string());
            }
        }
        for (const std::filesystem::path & includePath : includePaths)
        {
            ccArgs.push_back("-I" + includePath.string());
        }

        // Log ccArgs, each in a new line
        SPDLOG_DEBUG("ccArgs:");
        for (const auto & arg : ccArgs)
        {
            SPDLOG_DEBUG("{}", arg);
        }

        subprocess::Popen proc
        (
            ccArgs,
            subprocess::output{subprocess::PIPE},
            subprocess::error{subprocess::PIPE}
        );
        auto [out, err] = proc.communicate();

        std::string_view hierarchy(err.buf.data(), err.length);
        SPDLOG_DEBUG("Include hierarchy:\n{}", hierarchy);

        std::string includePath = parseStubIncludePath(hierarchy);
        return std::filesystem::canonical(includePath);
    }

    std::filesystem::path resolveSystemInclude(std::string_view includeName) const
    {
        return resolveInclude(true, includeName, {});
    }

    std::filesystem::path resolveUserInclude
    (
        std::string_view includeName, 
        const std::vector<std::filesystem::path> & parentPaths
    ) const
    {
        return resolveInclude(false, includeName, parentPaths);
    }

    // Get macros that are predefined by the compiler before processing any source file
    std::string getBuiltinMacros() const
    {
        // cc -dM -E - < /dev/null
        std::vector<std::string> ccArgs = {ccExePath, "-dM", "-E", "-"};
        subprocess::Popen proc
        (
            ccArgs,
            subprocess::input{"/dev/null"},
            subprocess::output{subprocess::PIPE},
            subprocess::error{subprocess::PIPE}
        );
        auto [out, err] = proc.communicate();
        return out.buf.data();
    }

    std::string getConcretelyExecutedMacros(const std::string & includePath) const
    {
        // cc -dM -E {includePath}
        std::vector<std::string> ccArgs = {ccExePath, "-dM", "-E", includePath};
        subprocess::Popen proc
        (
            ccArgs,
            subprocess::output{subprocess::PIPE},
            subprocess::error{subprocess::PIPE}
        );
        auto [out, err] = proc.communicate();
        return out.buf.data();
    }

private:
    std::string ccExePath;
    std::vector<std::filesystem::path> includePaths;

    // Parse the included filename from the first line of a "clang -H" output
    // An example of the output is:
    // . /usr/bin/../lib/gcc/x86_64-linux-gnu/11/../../../../include/c++/11/iostream
    // .. /usr/bin/../lib/gcc/x86_64-linux-gnu/11/../../../../include/x86_64-linux-gnu/c++/11/bits/c++config.h
    // ... /usr/bin/../lib/gcc/x86_64-linux-gnu/11/../../../../include/x86_64-linux-gnu/c++/11/bits/os_defines.h
    // .... /usr/include/features.h
    // ..... /usr/include/features-time64.h
    // ...... /usr/include/x86_64-linux-gnu/bits/wordsize.h
    // ...... /usr/include/x86_64-linux-gnu/bits/timesize.h
    // ....... /usr/include/x86_64-linux-gnu/bits/wordsize.h
    // ..... /usr/include/stdc-predef.h
    // ..... /usr/include/x86_64-linux-gnu/sys/cdefs.h
    // ...... /usr/include/x86_64-linux-gnu/bits/wordsize.h
    // ...... /usr/include/x86_64-linux-gnu/bits/long-double.h
    // ..... /usr/include/x86_64-linux-gnu/gnu/stubs.h
    // ...... /usr/include/x86_64-linux-gnu/gnu/stubs-64.h
    // ... /usr/bin/../lib/gcc/x86_64-linux-gnu/11/../../../../include/x86_64-linux-gnu/c++/11/bits/cpu_defines.h
    // ... /usr/bin/../lib/gcc/x86_64-linux-gnu/11/../../../../include/c++/11/pstl/pstl_config.h
    // .. /usr/bin/../lib/gcc/x86_64-linux-gnu/11/../../../../include/c++/11/ostream
    // It is a tree structure, but we only care about the first line of each include
    static std::string parseStubIncludePath(const std::string_view src)
    {
        SPDLOG_DEBUG("Parsing include hierarchy:\n{}", src.data());
        std::istringstream iss(src.data());
        std::string line;
        while (std::getline(iss, line))
        {
            if (!line.starts_with(". "))
            {
                continue;
            }
            SPDLOG_DEBUG("Parsed include: {}", line.substr(2).data());
            return line.substr(2);
        }
        return "";
    }
};

} // namespace Hayroll

#endif // HAYROLL_INCLUDERESOLVER_HPP
