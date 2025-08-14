#ifndef HAYROLL_REAPERWRAPPER_HPP
#define HAYROLL_REAPERWRAPPER_HPP

#include <string>
#include <filesystem>

#include <spdlog/spdlog.h>
#include "subprocess.hpp"
#include "json.hpp"

#include "Util.hpp"
#include "TempDir.hpp"

namespace Hayroll
{

class ReaperWrapper
{
public:
    static const std::string dummyCargoToml;

    static std::string runReaper
    (
        std::string_view seededRustStr
    )
    {
        // Example
        // reaper ./dir

        TempDir tempDir; // The input and output share the same directory
        std::filesystem::path tempDirPath = tempDir.getPath();
        std::filesystem::path inputFilePath = tempDirPath / "src/main.rs";
        std::filesystem::path cargoPath = tempDirPath / "Cargo.toml";
        saveStringToFile(dummyCargoToml, cargoPath);
        saveStringToFile(seededRustStr, inputFilePath);

        SPDLOG_TRACE
        (
            "Issuing command: {} {}",
            HayrollReaperExe.string(),
            tempDirPath.string()
        );
        
        subprocess::Popen reaperProcess
        (
            {
                HayrollReaperExe.string(),
                tempDirPath.string(),
            },
            subprocess::output{subprocess::PIPE},
            subprocess::error{subprocess::PIPE},
            subprocess::cwd{tempDirPath.string().c_str()}
        );

        auto [out, err] = reaperProcess.communicate();
        SPDLOG_TRACE("Reaper output:\n{}", out.buf.data());
        SPDLOG_TRACE("Reaper error:\n{}", err.buf.data());

        int retcode = reaperProcess.retcode();
        if (retcode != 0)
        {
            SPDLOG_ERROR("Reaper exited with code {}", retcode);
            // Include a snippet of stderr (avoid extremely long messages)
            std::string errStr = std::string(err.buf.data());
            if (errStr.size() > 2048) errStr = errStr.substr(0, 2048) + "...<truncated>";
            throw std::runtime_error("Reaper failed (exit code " + std::to_string(retcode) + "): " + errStr);
        }

        // Reaper rewrites the input file in place
        std::string rustStr = loadFileToString(inputFilePath);
        if (rustStr.empty())
        {
            throw std::runtime_error("Reaper produced an empty output file.");
        }
        return rustStr;
    }
};

const std::string ReaperWrapper::dummyCargoToml = R"(
[package]
name = "test"
version = "0.1.0"
edition = "2021"

[[bin]]
name = "test"
path = "src/main.rs"
)";

} // namespace Hayroll

#endif // HAYROLL_REAPERWRAPPER_HPP
