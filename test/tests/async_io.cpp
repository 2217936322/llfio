/* Integration test kernel for async i/o
(C) 2017 Niall Douglas <http://www.nedproductions.biz/> (2 commits)
File Created: Sept 2016


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

#include <future>

static inline void TestAsyncFileHandle()
{
  namespace afio = AFIO_V2_NAMESPACE;
  afio::io_service service;
  afio::async_file_handle h = afio::async_file_handle::async_file(service, {}, "temp", afio::file_handle::mode::write, afio::file_handle::creation::if_needed, afio::file_handle::caching::only_metadata, afio::file_handle::flag::unlink_on_close).value();
  std::vector<std::pair<std::future<afio::async_file_handle::const_buffers_type>, afio::async_file_handle::io_state_ptr>> futures;
  futures.reserve(1024);
  h.truncate(1024 * 4096).value();
  alignas(4096) char buffer[4096];
  memset(buffer, 78, 4096);                                               // NOLINT
  afio::async_file_handle::const_buffer_type bt{buffer, sizeof(buffer)};  // NOLINT
  for(size_t n = 0; n < 1024; n++)
  {
    std::promise<afio::async_file_handle::const_buffers_type> p;
    auto f(p.get_future());
    auto g(h
           .async_write({bt, n * 4096}, [ p = std::move(p), n ](afio::async_file_handle *, afio::async_file_handle::io_result<afio::async_file_handle::const_buffers_type> & result) mutable {
             (void) n;
             try
             {
               p.set_value(result.value());
               // std::cout << "Written block " << n << " successfully" << std::endl;
             }
             catch(...)
             {
               p.set_exception(std::current_exception());
               // std::cout << "Written block " << n << " unsuccessfully" << std::endl;
             }
           })
           .value());
    futures.emplace_back(std::move(f), std::move(g));
  }
  // Pump the i/o until no more work remains.
  while(service.run().value())
  {
  }
  // Make sure nothing went wrong by fetching the futures.
  for(auto &i : futures)
  {
    afio::async_file_handle::const_buffers_type out = i.first.get();
    // std::cout << out.data()->len << std::endl;
    BOOST_CHECK(out.data()->len == 4096);
  }
}

KERNELTEST_TEST_KERNEL(integration, afio, works, async_file_handle, "Tests that afio::async_file_handle works as expected", TestAsyncFileHandle())
