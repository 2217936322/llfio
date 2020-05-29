/* Integration test kernel for whether clone() works
(C) 2020 Niall Douglas <http://www.nedproductions.biz/> (2 commits)
File Created: May 2020


Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License in the accompanying file
Licence.txt or at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.


Distributed under the Boost Software License, Version 1.0.
    (See accompanying file Licence.txt or copy at
          http://www.boost.org/LICENSE_1_0.txt)
*/

#include "../test_kernel_decl.hpp"

#include <chrono>

#include "quickcpplib/algorithm/small_prng.hpp"
#include "quickcpplib/algorithm/string.hpp"

#ifndef _WIN32
#include <sys/resource.h>
#endif

static inline void TestCloneOrCopyFile()
{
  static constexpr size_t rounds = 100;
  static constexpr size_t max_file_extent = (size_t) 100 * 1024 * 1024;
  namespace llfio = LLFIO_V2_NAMESPACE;
  using QUICKCPPLIB_NAMESPACE::algorithm::small_prng::small_prng;
  const auto &tempdirh = llfio::path_discovery::storage_backed_temporary_files_directory();
  small_prng rand;
  for(size_t round = 0; round < rounds; round++)
  {
    auto srcfh = llfio::mapped_file_handle::mapped_temp_inode().value();
    auto maximum_extent = rand() % max_file_extent;
    srcfh.truncate(maximum_extent).value();
    for(uint8_t c = 1; c != 0; c++)
    {
      auto offset = rand() % maximum_extent;
      auto size = rand() % std::min(maximum_extent - offset, maximum_extent/8);
      llfio::byte buffer[65536];
      memset(&buffer, c, sizeof(buffer));
      for(unsigned n = 0; n < size; n += sizeof(buffer))
      {
        auto towrite = std::min((size_t) size - n, sizeof(buffer));
        srcfh.write(offset+n, {{buffer, towrite}}).value();
      }
    }

    auto randomname = llfio::utils::random_string(32);
    randomname.append(".random");
    llfio::algorithm::clone_or_copy(srcfh, tempdirh, randomname).value();

    auto destfh = llfio::mapped_file_handle::mapped_file(tempdirh, randomname).value();
    BOOST_REQUIRE(srcfh.maximum_extent().value() == destfh.maximum_extent().value());
    llfio::stat_t src_stat(nullptr), dest_stat(nullptr);
    src_stat.fill(srcfh).value();
    dest_stat.fill(destfh).value();
    std::cout << "Source file has " << src_stat.st_blocks << " blocks allocated. Destination file has " << dest_stat.st_blocks << " blocks allocated."
              << std::endl;
    BOOST_CHECK(src_stat.st_blocks == dest_stat.st_blocks);

    for(size_t n=0; n<maximum_extent; n++)
    {
      if(srcfh.address()[n] != destfh.address()[n])
      {
        std::cerr << "Byte at offset " << n << " is '" << *(char *) &srcfh.address()[n] << "' in source and is '" << *(char *) &destfh.address()[n]
                  << "' in destination." << std::endl;
        BOOST_CHECK(srcfh.address()[n] == destfh.address()[n]);
      }
    }
  }
}

#if 0
static inline void TestCloneOrCopyTree()
{
  static constexpr size_t rounds = 10;
#if defined(_WIN32) || defined(__APPLE__)
  static constexpr size_t total_entries = 100;  // create 100 directories in each random directory tree
#else
  static constexpr size_t total_entries = 1000;  // create 1000 directories in each random directory tree
#endif
  using namespace LLFIO_V2_NAMESPACE;
  using LLFIO_V2_NAMESPACE::file_handle;
  using QUICKCPPLIB_NAMESPACE::algorithm::small_prng::small_prng;
  using QUICKCPPLIB_NAMESPACE::algorithm::string::to_hex_string;
#ifndef _WIN32
  {
    struct rlimit r
    {
      1024 * 1024, 1024 * 1024
    };
    setrlimit(RLIMIT_NOFILE, &r);
  }
#endif
  small_prng rand;
  std::vector<directory_handle> dirhs;
  dirhs.reserve(total_entries);
  for(size_t round = 0; round < rounds; round++)
  {
    size_t entries_created = 1;
    dirhs.clear();
    dirhs.emplace_back(directory_handle::temp_directory().value());
    auto dirhpath = dirhs.front().current_path().value();
    std::cout << "\n\nCreating a random directory tree containing " << total_entries << " directories at " << dirhpath << " ..." << std::endl;
    auto begin = std::chrono::high_resolution_clock::now();
    for(size_t entries = 0; entries < total_entries; entries++)
    {
      const auto v = rand();
      const auto dir_idx = ((v >> 8) % dirhs.size());
      const auto file_entries = (v & 255);
      const auto &dirh = dirhs[dir_idx];
      filesystem::path::value_type buffer[10];
      {
        auto c = (uint32_t) entries;
        buffer[0] = 'd';
        to_hex_string(buffer + 1, 8, (const char *) &c, 4);
        buffer[9] = 0;
      }
      auto h = directory_handle::directory(dirh, path_view(buffer, 9, true), directory_handle::mode::write, directory_handle::creation::if_needed).value();
      entries_created++;
      for(size_t n = 0; n < file_entries; n++)
      {
        auto c = (uint8_t) n;
        buffer[0] = 'f';
        to_hex_string(buffer + 1, 2, (const char *) &c, 1);
        buffer[3] = 0;
        file_handle::file(h, path_view(buffer, 3, true), file_handle::mode::write, file_handle::creation::if_needed).value();
        entries_created++;
      }
      dirhs.emplace_back(std::move(h));
    }
    auto end = std::chrono::high_resolution_clock::now();
    dirhs.resize(1);
    std::cout << "Created " << entries_created << " filesystem entries in " << (std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count() / 1000000.0) << " seconds (which is " << (entries_created / (std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count() / 1000000.0))
              << " entries/sec).\n";

    auto summary = algorithm::summarize(dirhs.front()).value();
    std::cout << "Summary: " << summary.types[filesystem::file_type::regular] << " files and " << summary.types[filesystem::file_type::directory] << " directories created of " << summary.size << " bytes, " << summary.allocated << " bytes allocated in " << summary.blocks << " blocks with depth of " << summary.max_depth << "." << std::endl;
    BOOST_CHECK(summary.types[filesystem::file_type::regular] + summary.types[filesystem::file_type::directory] == entries_created);

    std::cout << "\nCalling llfio::algorithm::reduce() on that randomised directory tree ..." << std::endl;
    begin = std::chrono::high_resolution_clock::now();
    auto entries_removed = algorithm::reduce(std::move(dirhs.front())).value();
    end = std::chrono::high_resolution_clock::now();
    // std::cout << entries_removed << " " << entries_created << std::endl;
    BOOST_CHECK(entries_removed == entries_created);
    if(entries_removed != entries_created)
    {
      std::cout << "Entries created " << entries_created << ", entries removed " << entries_removed << std::endl;
    }
    std::cout << "Reduced " << entries_created << " filesystem entries in " << (std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count() / 1000000.0) << " seconds (which is " << (entries_created / (std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count() / 1000000.0))
              << " entries/sec).\n";

    log_level_guard g(log_level::fatal);
    auto r = directory_handle::directory({}, dirhpath);
    BOOST_REQUIRE(!r && r.error() == errc::no_such_file_or_directory);
  }
}
#endif

KERNELTEST_TEST_KERNEL(integration, llfio, algorithm, clone_or_copy_file, "Tests that llfio::algorithm::clone_or_copy(file_handle) works as expected", TestCloneOrCopyFile())
