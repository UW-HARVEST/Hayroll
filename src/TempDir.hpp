#ifndef HAYROLL_TEMPDIR_HPP
#define HAYROLL_TEMPDIR_HPP

#include <filesystem>
#include <string>
#include <chrono>
#include <random>
#include <system_error>

namespace Hayroll
{

class TempDir
{
public:
    operator const std::filesystem::path &() const noexcept
    {
        return path;
    }

    const std::filesystem::path & getPath() const noexcept
    {
        return path;
    }

    TempDir(const TempDir &) = delete;
    TempDir & operator=(const TempDir &) = delete;

    TempDir() : path(generateUniquePath())
    {
        createDir();
    }

    explicit TempDir(const std::filesystem::path & parent) 
        : path(parent / generateUniqueName())
    {
        createDir();
    }

    ~TempDir() noexcept
    {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }

private:
    std::filesystem::path path;

    static std::filesystem::path generateUniquePath()
    {
        return std::filesystem::temp_directory_path() / generateUniqueName();
    }

    static std::string generateUniqueName()
    {
        auto timestamp = std::chrono::system_clock::now().time_since_epoch().count();
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 9999);
        return "hayroll_" + std::to_string(timestamp) + "_" + std::to_string(dis(gen));
    }

    void createDir()
    {
        std::error_code ec;
        if (!std::filesystem::create_directory(path, ec))
        {
            throw std::runtime_error("Failed to create temp dir: " + ec.message());
        }
        std::filesystem::permissions
        (
            path, 
            std::filesystem::perms::owner_all,
            std::filesystem::perm_options::replace,
            ec
        );
    }
};

} // namespace Hayroll

#endif // HAYROLL_TEMPDIR_HPP
