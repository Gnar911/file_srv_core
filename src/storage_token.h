#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

#include "file_path_utils.h"


class StorageToken {
public:
    static constexpr std::string_view kDefaultAppName = "CBCM";

    explicit StorageToken(
        const std::string& token_id,
        std::string_view app_name = kDefaultAppName)
        : token_(token_id)
    {
        app_root_ = GetAppDataDirectoryForApp(app_name);
        temp_root_ = GetSystemTempDirectory();

        // Prefer committed storage if it already exists.
        if (!app_root_.empty() &&
            std::filesystem::exists(make_sqlite_path(app_root_)))
        {
            root_ = app_root_;
        }
        else
        {
            root_ = temp_root_;
        }
    }

    [[nodiscard]]
    const std::filesystem::path& root() const noexcept
    {
        return root_;
    }

    [[nodiscard]]
    const std::string& token() const noexcept
    {
        return token_;
    }

    [[nodiscard]]
    std::filesystem::path base_path() const
    {
        return root_ / token_;
    }

    [[nodiscard]]
    std::filesystem::path sqlite_path() const
    {
        return make_sqlite_path(root_);
    }

    [[nodiscard]]
    std::filesystem::path mmap_path() const
    {
        return make_mmap_path(root_);
    }

    [[nodiscard]]
    std::filesystem::path meta_path() const
    {
        return make_meta_path(root_);
    }

    [[nodiscard]]
    bool sqlite_exists() const
    {
        return std::filesystem::exists(sqlite_path());
    }

    [[nodiscard]]
    bool mmap_exists() const
    {
        return std::filesystem::exists(mmap_path());
    }

    [[nodiscard]]
    bool any_exists() const
    {
        return sqlite_exists() || mmap_exists();
    }

    [[nodiscard]]
    bool is_persistent() const noexcept
    {
        return root_ == app_root_;
    }

    /// Commit temporary storage into AppData.
    /// Safe to call multiple times.
    bool commit()
    {
        if (app_root_.empty()) {
            return false;
        }

        if (is_persistent()) {
            return true;
        }

        std::error_code ec;

        std::filesystem::create_directories(app_root_, ec);
        if (ec) {
            return false;
        }

        bool copied = false;

        copied |= copy_if_exists(
            make_sqlite_path(temp_root_),
            make_sqlite_path(app_root_));

        copied |= copy_if_exists(
            make_mmap_path(temp_root_),
            make_mmap_path(app_root_));

        copied |= copy_if_exists(
            make_meta_path(temp_root_),
            make_meta_path(app_root_));

        // Nothing was created yet.
        if (!copied) {
            return false;
        }

        // From now on use AppData.
        root_ = app_root_;
        return true;
    }

private:
    std::filesystem::path make_sqlite_path(
        const std::filesystem::path& root) const
    {
        return root / (token_ + ".index.sqlite");
    }

    std::filesystem::path make_mmap_path(
        const std::filesystem::path& root) const
    {
        return root / (token_ + ".mmap");
    }

    std::filesystem::path make_meta_path(
        const std::filesystem::path& root) const
    {
        return root / (token_ + ".meta");
    }

    static bool copy_if_exists(
        const std::filesystem::path& src,
        const std::filesystem::path& dst)
    {
        if (!std::filesystem::exists(src)) {
            return false;
        }

        std::error_code ec;

        std::filesystem::copy_file(
            src,
            dst,
            std::filesystem::copy_options::overwrite_existing,
            ec);

        return !ec;
    }

private:
    std::string token_;

    std::filesystem::path root_;
    std::filesystem::path temp_root_;
    std::filesystem::path app_root_;
};

