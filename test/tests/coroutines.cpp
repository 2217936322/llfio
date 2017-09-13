/* Integration test kernel for map_handle create and close
(C) 2016-2017 Niall Douglas <http://www.nedproductions.biz/> (2 commits)
File Created: Aug 2016


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

#include "../../include/afio/afio.hpp"
#include "kerneltest/include/kerneltest.hpp"

#include <future>

static inline void TestAsyncFileHandleCoroutines()
{
  namespace afio = AFIO_V2_NAMESPACE;
#ifdef __cpp_coroutines
  afio::io_service service;
  afio::async_file_handle h = afio::async_file_handle::async_file(service, {}, "temp", afio::file_handle::mode::write, afio::file_handle::creation::if_needed, afio::file_handle::caching::only_metadata, afio::file_handle::flag::unlink_on_close).value();
  h.truncate(1024 * 4096);
  // Launch 8 coroutines, each writing 4Kb of chars 0-8 to every 32Kb block
  auto coroutine = [&h](size_t no) -> std::future<void> {
    alignas(4096) char buffer[4096];
    memset(buffer, (int) ('0' + no), 4096);
    afio::async_file_handle::const_buffer_type bt{buffer};
    for(size_t n = 0; n < 128; n++)
    {
      auto written = co_await h.co_write({bt, n * 32768 + no * 4096});
      written.value();
    }
  };
  std::vector<std::future<void>> coroutines;
  for(size_t n = 0; n < 8; n++)
  {
    coroutines.push_back(coroutine(n));
  }
  // Pump the i/o, multiplexing the coroutines, until no more work remains
  while(service.run().value())
    ;
  // Make sure nothing went wrong
  for(auto &i : coroutines)
  {
    i.get();
  }
  // Check that the file has the right contents
  alignas(4096) char buffer1[4096], buffer2[4096];
  afio::async_file_handle::extent_type offset = 0;
  for(size_t n = 0; n < 128; n++)
  {
    for(size_t m = 0; m < 8; m++)
    {
      memset(buffer2, (int) ('0' + m), 4096);
      h.read(offset, buffer1, 4096).value();
      BOOST_CHECK(!memcmp(buffer1, buffer2, 4096));
      offset += 4096;
    }
  }
#endif
}

KERNELTEST_TEST_KERNEL(integration, afio, coroutines, async_file_handle, "Tests that afio::async_file_handle works as expected with Coroutines", TestAsyncFileHandleCoroutines())
