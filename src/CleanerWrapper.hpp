#ifndef HAYROLL_CLEANERWRAPPER_HPP
#define HAYROLL_CLEANERWRAPPER_HPP

#include <string>
#include <filesystem>

#include <spdlog/spdlog.h>
#include "subprocess.hpp"
#include "json.hpp"

#include "Util.hpp"
#include "TempDir.hpp"

namespace Hayroll
{

class CleanerWrapper
{
public:
    static const std::string dummyCargoToml;

    static std::string runCleaner
    (
        std::string_view rustStr
    )
    {
        TempDir tempDir;
        std::filesystem::path tempDirPath = tempDir.getPath();
        std::filesystem::path inputFilePath = tempDirPath / "src/main.rs";
        std::filesystem::path cargoPath = tempDirPath / "Cargo.toml";
        saveStringToFile(dummyCargoToml, cargoPath);
        saveStringToFile(rustStr, inputFilePath);

        SPDLOG_TRACE
        (
            "Issuing command: {} {}",
            HayrollCleanerExe.string(),
            tempDirPath.string()
        );

        subprocess::Popen cleanerProcess
        (
            {
                HayrollCleanerExe.string(),
                tempDirPath.string(),
            },
            subprocess::output{subprocess::PIPE},
            subprocess::error{subprocess::PIPE},
            subprocess::cwd{tempDirPath.string().c_str()}
        );

        auto [out, err] = cleanerProcess.communicate();
        SPDLOG_TRACE("Cleaner stdout:\n{}", out.buf.data());
        SPDLOG_TRACE("Cleaner stderr:\n{}", err.buf.data());

        int retcode = cleanerProcess.retcode();
        if (retcode != 0)
        {
            SPDLOG_ERROR("Cleaner exited with code {}", retcode);
            std::string errStr = std::string(out.buf.data()) + '\n' + std::string(err.buf.data());
            throw std::runtime_error("Cleaner failed (exit code " + std::to_string(retcode) + "): " + errStr);
        }

        std::string cleanedRustStr = loadFileToString(inputFilePath);
        if (cleanedRustStr.empty())
        {
            throw std::runtime_error("Cleaner produced an empty output file.");
        }
        return cleanedRustStr;
    }
};

const std::string CleanerWrapper::dummyCargoToml = R"(
[package]
name = "test"
version = "0.1.0"
edition = "2021"

[[bin]]
name = "test"
path = "src/main.rs"
)";

} // namespace Hayroll

#endif // HAYROLL_CLEANERWRAPPER_HPP
