#include <iostream>
#include <filesystem>

#include <spdlog/spdlog.h>

#include "TempDir.hpp"

int main(int argc, char **argv)
{
    using namespace Hayroll;

    spdlog::set_level(spdlog::level::debug);

    // Test TempDir by creating 10 temporary directories
    for (int i = 0; i < 10; i++)
    {
        auto tmpPath = std::filesystem::temp_directory_path();
        {
            auto tmpDir = TempDir();
            tmpPath = tmpDir.getPath();
            std::cout << "Created:" << tmpPath << std::endl;
        }
        // Check that the directory is removed
        if (std::filesystem::exists(tmpPath))
        {
            std::cout << "Not removed: " << tmpPath << std::endl;
            return 1;
        }
        else
        {
            std::cout << "Removed: " << tmpPath << std::endl;
        }
    }
    return 0;
}
