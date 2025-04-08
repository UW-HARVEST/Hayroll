// Utility class for creating and managing temporary directories

#ifndef HAYROLL_TEMPDIR_HPP
#define HAYROLL_TEMPDIR_HPP

#include <filesystem>
#include <string>
#include <chrono>
#include <random>
#include <system_error>
#include <memory>

#include "Util.hpp"

namespace Hayroll
{

class TempDir
{
public:
    TempDir(bool autoDelete = true)
        : path(generateUniquePath()), pathPtr(&path, autoDelete ? &deleteDir : &keepDir)
    {
        createDir();
    }

    TempDir(const TempDir &) = delete;

    TempDir(TempDir && src)
        : path(src.path), pathPtr(&path, &deleteDir)
    {
        src.pathPtr.release();
    }

    TempDir & operator=(const TempDir &) = delete;

    TempDir & operator=(TempDir && src)
    {
        if (this != &src)
        {
            path = src.path;
            pathPtr.reset(&path);
            src.pathPtr.release();
        }
        return *this;
    }

    ~TempDir() = default;

    operator const std::filesystem::path &() const noexcept
    {
        return path;
    }

    const std::filesystem::path & getPath() const noexcept
    {
        return path;
    }

    explicit TempDir(const std::filesystem::path & parent, bool autoDelete = true)
        : path(parent / generateUniqueName()), pathPtr(&path, autoDelete ? &deleteDir : &keepDir)
    {
        createDir();
    }

    static void deleteDir(const std::filesystem::path * path)
    {
        std::error_code ec;
        std::filesystem::remove_all(*path, ec);
    }

    static void keepDir(const std::filesystem::path * path)
    {
        // Do nothing, just keep the directory
    }

private:
    std::filesystem::path path;
    std::unique_ptr<std::filesystem::path, decltype(&deleteDir)> pathPtr;

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
