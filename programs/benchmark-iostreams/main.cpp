/* Test the latency of iostreams vs AFIO
(C) 2018 Niall Douglas <http://www.nedproductions.biz/> (6 commits)
File Created: Apr 2018


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

#define MAXBLOCKSIZE (256 * 1024)
#define REGIONSIZE (100 * 1024 * 1024)

#include "../../include/afio/afio.hpp"
#if __has_include("quickcpplib/include/algorithm/small_prng.hpp")
#include "quickcpplib/include/algorithm/small_prng.hpp"
#else
#include "../../include/afio/v2.0/quickcpplib/include/algorithm/small_prng.hpp"
#endif

#include <chrono>
#include <fstream>
#include <iostream>
#include <vector>

namespace afio = AFIO_V2_NAMESPACE;
using QUICKCPPLIB_NAMESPACE::algorithm::small_prng::small_prng;

uint64_t nanoclock()
{
#ifdef _MSC_VER
  auto rdtscp = [] {
    unsigned x;
    return (uint64_t) __rdtscp(&x);
  };
#else
#ifdef __rdtscp
  return (uint64_t) __rdtscp();
#elif defined(__x86_64__)
  auto rdtscp = [] {
    unsigned lo, hi;
    asm volatile("rdtscp" : "=a"(lo), "=d"(hi));
    return (uint64_t) lo | ((uint64_t) hi << 32);
  };
#elif defined(__i386__)
  auto rdtscp = [] {
    unsigned count;
    asm volatile("rdtscp" : "=a"(count));
    return (uint64_t) count;
  };
#endif
#if __ARM_ARCH >= 6
  auto rdtscp = [] {
    unsigned count;
    asm volatile("MRC p15, 0, %0, c9, c13, 0" : "=r"(count));
    return (uint64_t) count * 64;
  };
#endif
#endif

  static uint16_t ticks_per_sec;
  static uint64_t offset;
  if(ticks_per_sec == 0)
  {
    auto end = std::chrono::high_resolution_clock::now(), begin = std::chrono::high_resolution_clock::now();
    auto diff = std::chrono::duration_cast<std::chrono::seconds>(end - begin);
    uint64_t _begin = rdtscp(), _end;
    do
    {
      end = std::chrono::high_resolution_clock::now();
    } while(std::chrono::duration_cast<std::chrono::seconds>(end - begin).count() < 1);
    _end = rdtscp();
    uint64_t x = _end - _begin;
    x /= (1000000000 / 128);
    ticks_per_sec = (uint16_t) x;
    volatile uint64_t a = (uint64_t)((128 * rdtscp()) / ticks_per_sec);
    volatile uint64_t b = (uint64_t)((128 * rdtscp()) / ticks_per_sec);
    offset = b - a;
#if 1
    std::cout << "There are " << (ticks_per_sec / 128.0) << " TSCs in 1 nanosecond and it takes " << offset << " nanoseconds per nanoclock()." << std::endl;
#endif
  }
  return (uint64_t)((128 * rdtscp()) / ticks_per_sec) - offset;
}

template <class F> inline void run_test(const char *csv, off_t max_extent, F &&f)
{
  char buffer[MAXBLOCKSIZE];
  std::vector<std::pair<unsigned, unsigned>> offsets(512 * 1024);
  std::vector<std::vector<unsigned>> results;
  for(size_t blocksize = 1; blocksize <= MAXBLOCKSIZE; blocksize <<= 1)
  {
    size_t scale = blocksize / 16;
    if(scale < 1)
      scale = 1;
    small_prng rand;
    for(auto &i : offsets)
    {
      i.first = rand() % (max_extent - MAXBLOCKSIZE);
    }
    memset(buffer, 0, sizeof(buffer));
    for(size_t n = 0; n < offsets.size() / scale; n++)
    {
      auto begin = nanoclock();
      f(offsets[n].first, buffer, blocksize);
      auto end = nanoclock();
      offsets[n].second = (unsigned int) (end - begin);
    }
    results.emplace_back();
    for(size_t n = 0; n < offsets.size() / scale; n++)
    {
      results.back().push_back(offsets[n].second);
    }
  }
  std::ofstream out(csv);
  for(size_t blocksize = 1; blocksize <= MAXBLOCKSIZE; blocksize <<= 1)
  {
    out << "," << blocksize;
  }
  out << std::endl;
  for(size_t n = 0; n < offsets.size(); n++)
  {
    auto it = results.cbegin();
    for(size_t blocksize = 1; blocksize <= MAXBLOCKSIZE; blocksize <<= 1, ++it)
    {
      if(n < it->size())
        out << "," << it->at(n);
    }
    out << std::endl;
  }
}

int main()
{
  {
    auto th = afio::file({}, "testfile", afio::file_handle::mode::write, afio::file_handle::creation::if_needed).value();
    std::vector<char> buffer(REGIONSIZE, 'a');
    th.write(0, {{(afio::byte *) buffer.data(), buffer.size()}}).value();
    th.barrier({}, true, true).value();
  }
  {
    auto begin = nanoclock();
    while(nanoclock() - begin < 1000000000ULL)
      ;
  }
#if 0
  {
    std::cout << "Testing latency of afio::file_handle with random malloc/free ..." << std::endl;
    auto th = afio::file({}, "testfile").value();
    std::vector<void *> allocations(1024 * 1024);
    small_prng rand;
    for(auto &i : allocations)
    {
      i = malloc(rand() % 4096);
    }
    run_test("file_handle_malloc_free.csv", 1024 * 1024, [&](unsigned offset, char *buffer, size_t len) {
      th.read(offset, {{(afio::byte *) buffer, len}}).value();
      for(size_t n = 0; n < rand() % 64; n++)
      {
        size_t i = rand() % (1024 * 1024);
        if(allocations[i] == nullptr)
          allocations[i] = malloc(rand() % 4096);
        else
        {
          free(allocations[i]);
          allocations[i] = nullptr;
        }
      }
    });
  }
#endif
#if 1
  {
    std::cout << "Testing latency of iostreams ..." << std::endl;
    std::ifstream testfile("testfile");
    testfile.exceptions(std::ios::failbit | std::ios::badbit);
    run_test("iostreams.csv", REGIONSIZE, [&](unsigned offset, char *buffer, size_t len) {
      testfile.seekg(offset, std::ios::beg);
      testfile.read(buffer, len);
    });
  }
#endif
  {
    std::cout << "Testing latency of afio::file_handle ..." << std::endl;
    auto th = afio::file({}, "testfile").value();
    run_test("file_handle.csv", REGIONSIZE, [&](unsigned offset, char *buffer, size_t len) { th.read(offset, {{(afio::byte *) buffer, len}}).value(); });
  }
#if 1
  {
    std::cout << "Testing latency of afio::mapped_file_handle ..." << std::endl;
    auto th = afio::mapped_file({}, "testfile").value();
    run_test("mapped_file_handle.csv", REGIONSIZE, [&](unsigned offset, char *buffer, size_t len) { th.read(offset, {{(afio::byte *) buffer, len}}).value(); });
  }
#endif
#if 1
  {
    std::cout << "Testing latency of memcpy ..." << std::endl;
    auto th = afio::map(REGIONSIZE).value();
#if 1
    {
      // Prefault
      volatile char *p = (char *) th.address(), x;
      for(size_t n = 0; n < REGIONSIZE; n += 64)
      {
        x = p[n];
      }
    }
#endif
    run_test("memcpy.csv", REGIONSIZE, [&](unsigned offset, char *buffer, size_t len) {
#if 0
      memcpy(buffer, th.address() + offset, len);
#else
      // Can't use memcpy, it gets elided
      const afio::byte *__restrict s = th.address() + offset;
#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
      while(len >= 4 * sizeof(__m128i))
      {
        __m128i a = *(const __m128i *__restrict) s;
        s += sizeof(__m128i);
        __m128i b = *(const __m128i *__restrict) s;
        s += sizeof(__m128i);
        __m128i c = *(const __m128i *__restrict) s;
        s += sizeof(__m128i);
        __m128i d = *(const __m128i *__restrict) s;
        s += sizeof(__m128i);
        *(__m128i * __restrict) buffer = a;
        buffer += sizeof(__m128i);
        *(__m128i * __restrict) buffer = b;
        buffer += sizeof(__m128i);
        *(__m128i * __restrict) buffer = c;
        buffer += sizeof(__m128i);
        *(__m128i * __restrict) buffer = d;
        buffer += sizeof(__m128i);
        len -= 4 * sizeof(__m128i);
      }
      while(len >= sizeof(__m128i))
      {
        *(__m128i * __restrict) buffer = *(const __m128i *__restrict) s;
        buffer += sizeof(__m128i);
        s += sizeof(__m128i);
        len -= sizeof(__m128i);
      }
#endif
      while(len >= sizeof(uint64_t))
      {
        *(volatile uint64_t * __restrict) buffer = *(const uint64_t *__restrict) s;
        buffer += sizeof(uint64_t);
        s += sizeof(uint64_t);
        len -= sizeof(uint64_t);
      }
      if(len >= sizeof(uint32_t))
      {
        *(volatile uint32_t * __restrict) buffer = *(const uint32_t *__restrict) s;
        buffer += sizeof(uint32_t);
        s += sizeof(uint32_t);
        len -= sizeof(uint32_t);
      }
      if(len >= sizeof(uint16_t))
      {
        *(volatile uint16_t * __restrict) buffer = *(const uint16_t *__restrict) s;
        buffer += sizeof(uint16_t);
        s += sizeof(uint16_t);
        len -= sizeof(uint16_t);
      }
      if(len >= sizeof(uint8_t))
      {
        *(volatile uint8_t * __restrict) buffer = *(const uint8_t *__restrict) s;
        buffer += sizeof(uint8_t);
        s += sizeof(uint8_t);
        len -= sizeof(uint8_t);
      }
#endif
    });
  }
#endif
  afio::filesystem::remove("testfile");
}
