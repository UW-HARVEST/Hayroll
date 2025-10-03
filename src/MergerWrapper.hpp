#ifndef HAYROLL_MERGERWRAPPER_HPP
#define HAYROLL_MERGERWRAPPER_HPP

#include <string>
#include <filesystem>

#include <spdlog/spdlog.h>
#include "subprocess.hpp"
#include "json.hpp"

#include "Util.hpp"
#include "TempDir.hpp"

namespace Hayroll
{

class MergerWrapper
{
public:
    static const std::string dummyCargoToml;

    static std::string runMerger
    (
        std::string_view reapedRustStrBase,
        std::string_view reapedRustStrPatch
    )
    {
        // Example
        // merger ./dir_base ./dir_patch

        TempDir tempDirBase; // The input and output share the same directory
        std::filesystem::path tempDirBasePath = tempDirBase.getPath();
        std::filesystem::path inputFileBasePath = tempDirBasePath / "src/main.rs";
        std::filesystem::path cargoBasePath = tempDirBasePath / "Cargo.toml";
        saveStringToFile(dummyCargoToml, cargoBasePath);
        saveStringToFile(reapedRustStrBase, inputFileBasePath);

        TempDir tempDirPatch;
        std::filesystem::path tempDirPatchPath = tempDirPatch.getPath();
        std::filesystem::path inputFilePatchPath = tempDirPatchPath / "src/main.rs";
        std::filesystem::path cargoPatchPath = tempDirPatchPath / "Cargo.toml";
        saveStringToFile(dummyCargoToml, cargoPatchPath);
        saveStringToFile(reapedRustStrPatch, inputFilePatchPath);

        SPDLOG_TRACE
        (
            "Issuing command: {} {}",
            HayrollMergerExe.string(),
            tempDirBasePath.string(),
            tempDirPatchPath.string()
        );
        
        subprocess::Popen mergerProcess
        (
            {
                HayrollMergerExe.string(),
                tempDirBasePath.string(),
                tempDirPatchPath.string(),
            },
            subprocess::output{subprocess::PIPE},
            subprocess::error{subprocess::PIPE},
            subprocess::cwd{tempDirBasePath.string().c_str()}
        );

        auto [out, err] = mergerProcess.communicate();
        SPDLOG_TRACE("Merger stdout:\n{}", out.buf.data());
        SPDLOG_TRACE("Merger stderr:\n{}", err.buf.data());

        int retcode = mergerProcess.retcode();
        if (retcode != 0)
        {
            SPDLOG_ERROR("Merger exited with code {}", retcode);
            // Include a snippet of stderr (avoid extremely long messages)
            std::string errStr = std::string(out.buf.data()) + '\n' + std::string(err.buf.data());
            throw std::runtime_error("Merger failed (exit code " + std::to_string(retcode) + "): " + errStr);
        }

        // Merger rewrites the input file in place
        std::string rustStr = loadFileToString(inputFileBasePath);
        if (rustStr.empty())
        {
            throw std::runtime_error("Merger produced an empty output file.");
        }
        return rustStr;
    }
};

const std::string MergerWrapper::dummyCargoToml = R"(
[package]
name = "test"
version = "0.1.0"
edition = "2021"

[[bin]]
name = "test"
path = "src/main.rs"
)";

} // namespace Hayroll

#endif // HAYROLL_MERGERWRAPPER_HPP
