//===----------------------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// UNSUPPORTED: c++98, c++03

// <experimental/filesystem>

// void copy(const path& from, const path& to);
// void copy(const path& from, const path& to, error_code& ec) noexcept;
// void copy(const path& from, const path& to, copy_options options);
// void copy(const path& from, const path& to, copy_options options,
//           error_code& ec) noexcept;

#include <experimental/filesystem>
#include <type_traits>
#include <cassert>

#include "test_macros.h"
#include "rapid-cxx-test.hpp"
#include "filesystem_test_helper.hpp"

using namespace std::experimental::filesystem;
namespace fs = std::experimental::filesystem;

using CO = fs::copy_options;

TEST_SUITE(filesystem_copy_test_suite)

TEST_CASE(signature_test)
{
    const path p; ((void)p);
    std::error_code ec; ((void)ec);
    const copy_options opts{}; ((void)opts);
    ASSERT_NOT_NOEXCEPT(fs::copy(p, p));
    ASSERT_NOEXCEPT(fs::copy(p, p, ec));
    ASSERT_NOT_NOEXCEPT(copy(p, p, opts));
    ASSERT_NOEXCEPT(copy(p, p, opts, ec));
}

// There are 4 cases is the proposal for absolute path.
// Each scope tests one of the cases.
TEST_CASE(test_error_reporting)
{
    auto checkThrow = [](path const& f, path const& t, const std::error_code& ec)
    {
#ifndef TEST_HAS_NO_EXCEPTIONS
        try {
            fs::copy(f, t);
            return false;
        } catch (filesystem_error const& err) {
            return err.path1() == f
                && err.path2() == t
                && err.code() == ec;
        }
#else
        return true;
#endif
    };

    scoped_test_env env;
    const path file = env.create_file("file1", 42);
    const path dir = env.create_dir("dir");
    const path fifo = env.create_fifo("fifo");
    TEST_REQUIRE(is_other(fifo));

    // !exists(f)
    {
        std::error_code ec;
        const path f = StaticEnv::DNE;
        const path t = env.test_root;
        fs::copy(f, t, ec);
        TEST_REQUIRE(ec);
        TEST_CHECK(checkThrow(f, t, ec));
    }
    { // equivalent(f, t) == true
        std::error_code ec;
        fs::copy(file, file, ec);
        TEST_REQUIRE(ec);
        TEST_CHECK(checkThrow(file, file, ec));
    }
    { // is_directory(from) && is_file(to)
        std::error_code ec;
        fs::copy(dir, file, ec);
        TEST_REQUIRE(ec);
        TEST_CHECK(checkThrow(dir, file, ec));
    }
    { // is_other(from)
        std::error_code ec;
        fs::copy(fifo, dir, ec);
        TEST_REQUIRE(ec);
        TEST_CHECK(checkThrow(fifo, dir, ec));
    }
    { // is_other(to)
        std::error_code ec;
        fs::copy(file, fifo, ec);
        TEST_REQUIRE(ec);
        TEST_CHECK(checkThrow(file, fifo, ec));
    }
}

TEST_CASE(from_is_symlink)
{
    scoped_test_env env;
    const path file = env.create_file("file", 42);
    const path symlink = env.create_symlink(file, "sym");
    const path dne = env.make_env_path("dne");

    { // skip symlinks
        std::error_code ec = GetTestEC();
        fs::copy(symlink, dne, copy_options::skip_symlinks, ec);
        TEST_CHECK(!ec);
        TEST_CHECK(!exists(dne));
    }
    {
        const path dest = env.make_env_path("dest");
        std::error_code ec = GetTestEC();
        fs::copy(symlink, dest, copy_options::copy_symlinks, ec);
        TEST_CHECK(!ec);
        TEST_CHECK(exists(dest));
        TEST_CHECK(is_symlink(dest));
    }
    { // copy symlink but target exists
        std::error_code ec = GetTestEC();
        fs::copy(symlink, file, copy_options::copy_symlinks, ec);
        TEST_CHECK(ec);
    }
    { // create symlinks but target exists
        std::error_code ec = GetTestEC();
        fs::copy(symlink, file, copy_options::create_symlinks, ec);
        TEST_CHECK(ec);
    }
}

TEST_CASE(from_is_regular_file)
{
    scoped_test_env env;
    const path file = env.create_file("file", 42);
    const path dir = env.create_dir("dir");
    { // skip copy because of directory
        const path dest = env.make_env_path("dest1");
        std::error_code ec = GetTestEC();
        fs::copy(file, dest, CO::directories_only, ec);
        TEST_CHECK(!ec);
        TEST_CHECK(!exists(dest));
    }
    { // create symlink to file
        const path dest = env.make_env_path("sym");
        std::error_code ec = GetTestEC();
        fs::copy(file, dest, CO::create_symlinks, ec);
        TEST_CHECK(!ec);
        TEST_CHECK(is_symlink(dest));
        TEST_CHECK(equivalent(file, canonical(dest)));
    }
    { // create hard link to file
        const path dest = env.make_env_path("hardlink");
        TEST_CHECK(hard_link_count(file) == 1);
        std::error_code ec = GetTestEC();
        fs::copy(file, dest, CO::create_hard_links, ec);
        TEST_CHECK(!ec);
        TEST_CHECK(exists(dest));
        TEST_CHECK(hard_link_count(file) == 2);
    }
    { // is_directory(t)
        const path dest_dir = env.create_dir("dest_dir");
        const path expect_dest = dest_dir / file.filename();
        std::error_code ec = GetTestEC();
        fs::copy(file, dest_dir, ec);
        TEST_CHECK(!ec);
        TEST_CHECK(is_regular_file(expect_dest));
    }
    { // otherwise copy_file(from, to, ...)
        const path dest = env.make_env_path("file_copy");
        std::error_code ec = GetTestEC();
        fs::copy(file, dest, ec);
        TEST_CHECK(!ec);
        TEST_CHECK(is_regular_file(dest));
    }
}

TEST_CASE(from_is_directory)
{
    struct FileInfo {
        path filename;
        int size;
    };
    const FileInfo files[] = {
        {"file1", 0},
        {"file2", 42},
        {"file3", 300}
    };
    scoped_test_env env;
    const path dir = env.create_dir("dir");
    const path nested_dir_name = "dir2";
    const path nested_dir = env.create_dir("dir/dir2");

    for (auto& FI : files) {
        env.create_file(dir / FI.filename, FI.size);
        env.create_file(nested_dir / FI.filename, FI.size);
    }
    { // test for non-existent directory
        const path dest = env.make_env_path("dest_dir1");
        std::error_code ec = GetTestEC();
        fs::copy(dir, dest, ec);
        TEST_REQUIRE(!ec);
        TEST_CHECK(is_directory(dest));
        for (auto& FI : files) {
            path created = dest / FI.filename;
            TEST_CHECK(is_regular_file(created));
            TEST_CHECK(file_size(created) == FI.size);
        }
        TEST_CHECK(!is_directory(dest / nested_dir_name));
    }
    { // test for existing directory
        const path dest = env.create_dir("dest_dir2");
        std::error_code ec = GetTestEC();
        fs::copy(dir, dest, ec);
        TEST_REQUIRE(!ec);
        TEST_CHECK(is_directory(dest));
        for (auto& FI : files) {
            path created = dest / FI.filename;
            TEST_CHECK(is_regular_file(created));
            TEST_CHECK(file_size(created) == FI.size);
        }
        TEST_CHECK(!is_directory(dest / nested_dir_name));
    }
    { // test recursive copy
        const path dest = env.make_env_path("dest_dir3");
        std::error_code ec = GetTestEC();
        fs::copy(dir, dest, CO::recursive, ec);
        TEST_REQUIRE(!ec);
        TEST_CHECK(is_directory(dest));
        const path nested_dest = dest / nested_dir_name;
        TEST_REQUIRE(is_directory(nested_dest));
        for (auto& FI : files) {
            path created = dest / FI.filename;
            path nested_created = nested_dest / FI.filename;
            TEST_CHECK(is_regular_file(created));
            TEST_CHECK(file_size(created) == FI.size);
            TEST_CHECK(is_regular_file(nested_created));
            TEST_CHECK(file_size(nested_created) == FI.size);
        }
    }

}
TEST_SUITE_END()
