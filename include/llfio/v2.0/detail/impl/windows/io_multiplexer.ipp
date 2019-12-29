/* Multiplex file i/o
(C) 2019 Niall Douglas <http://www.nedproductions.biz/> (9 commits)
File Created: Nov 2019


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

#include "../../../io_multiplexer.hpp"

#include "import.hpp"

#ifndef _WIN32
#error This implementation file is for Microsoft Windows only
#endif

#include <chrono>
#include <map>
#include <unordered_map>

LLFIO_V2_NAMESPACE_BEGIN

namespace detail
{
#if LLFIO_EXPERIMENTAL_STATUS_CODE
#else
  LLFIO_HEADERS_ONLY_FUNC_SPEC error_info ntkernel_error_from_overlapped(size_t code) { return ntkernel_error((NTSTATUS) code); }
#endif
}  // namespace detail

template <bool threadsafe> class win_iocp_impl final : public io_multiplexer_impl<threadsafe>
{
  using _base = io_multiplexer_impl<threadsafe>;
  using _lock_guard = typename _base::_lock_guard;
  template <class T> using atomic_type = std::conditional_t<threadsafe, std::atomic<T>, detail::fake_atomic<T>>;

  static_assert(sizeof(typename detail::io_operation_connection::_OVERLAPPED) == sizeof(OVERLAPPED), "detail::io_operation_connection::_OVERLAPPED does not match OVERLAPPED!");

  atomic_type<size_t> _concurrent_run_instances{0};  // how many threads inside wait() there are right now
  atomic_type<size_t> _total_pending_io{0};          // how many pending i/o there are right now
  // Linked list of all deadlined i/o's pending completion
  detail::io_operation_connection *_pending_begin{nullptr}, *_pending_end{nullptr};

  // ONLY if _do_timeout_io() is called
  std::multimap<std::chrono::steady_clock::time_point, detail::io_operation_connection *> _durations;
  std::multimap<std::chrono::system_clock::time_point, detail::io_operation_connection *> _absolutes;

public:
  result<void> init(size_t threads)
  {
    this->_v.h = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, (DWORD) threads);
    if(nullptr == this->_v.h)
    {
      return win32_error();
    }
    return success();
  }
  virtual ~win_iocp_impl()
  {
    this->_lock.lock();
    if(_total_pending_io.load(std::memory_order_acquire) > 0)
    {
      LLFIO_LOG_FATAL(nullptr, "win_iocp_impl::~win_iocp_impl() called with i/o handles still doing work");
      abort();
    }
    (void) CloseHandle(this->_v.h);
  }

  virtual void _post(function_ptr<void *(void *)> &&f) noexcept override final
  {
    _base::_post(std::move(f));
    // Poke IOCP to wake
    PostQueuedCompletionStatus(this->_v.h, 0, 0, nullptr);
  }

  virtual result<void> _register_io_handle(handle *h) noexcept override final
  {
    windows_nt_kernel::init();
    using namespace windows_nt_kernel;
    LLFIO_LOG_FUNCTION_CALL(this);
    IO_STATUS_BLOCK isb = make_iostatus();
    FILE_COMPLETION_INFORMATION fci{};
    memset(&fci, 0, sizeof(fci));
    fci.Port = this->_v.h;
    fci.Key = (void *) 1;  // not null
    NTSTATUS ntstat = NtSetInformationFile(h->native_handle().h, &isb, &fci, sizeof(fci), FileCompletionInformation);
    if(STATUS_PENDING == ntstat)
    {
      ntstat = ntwait(h->native_handle().h, isb, deadline());
    }
    if(ntstat < 0)
    {
      return ntkernel_error(ntstat);
    }
    // Don't wake run() for i/o which completes immediately. We ignore
    // failure as not all handles support this, and we are idempotent to
    // spurious wakes in any case.
    SetFileCompletionNotificationModes(h->native_handle().h, FILE_SKIP_COMPLETION_PORT_ON_SUCCESS | FILE_SKIP_SET_EVENT_ON_HANDLE);
    return success();
  }
  virtual result<void> _deregister_io_handle(handle *h) noexcept override final
  {
    windows_nt_kernel::init();
    using namespace windows_nt_kernel;
    LLFIO_LOG_FUNCTION_CALL(this);
    IO_STATUS_BLOCK isb = make_iostatus();
    FILE_COMPLETION_INFORMATION fci{};
    memset(&fci, 0, sizeof(fci));
    fci.Port = nullptr;
    fci.Key = nullptr;
    NTSTATUS ntstat = NtSetInformationFile(h->native_handle().h, &isb, &fci, sizeof(fci), FileReplaceCompletionInformation);
    if(STATUS_PENDING == ntstat)
    {
      ntstat = ntwait(h->native_handle().h, isb, deadline());
    }
    if(ntstat < 0)
    {
      return ntkernel_error(ntstat);
    }
    return success();
  }

  virtual result<int> invoke_posted_items(int max_items = -1, deadline d = deadline()) noexcept override final
  {
    LLFIO_LOG_FUNCTION_CALL(this);
    return this->_execute_posted_items(max_items, d);
  }

  result<span<detail::io_operation_connection *>> _do_timeout_io(LARGE_INTEGER &_timeout, LARGE_INTEGER *&timeout, bool &need_to_wake_all, span<detail::io_operation_connection *> in) noexcept
  {
    try
    {
      span<detail::io_operation_connection *> out(in.data(), (size_t) 0);
      for(auto *i = _pending_end; i != nullptr && !i->is_added_to_deadline_list; i = i->prev)
      {
        if(i->deadline_absolute != std::chrono::system_clock::time_point())
        {
          auto it = _absolutes.insert({i->deadline_absolute, i});
          if(it == _absolutes.begin())
          {
            need_to_wake_all = true;
          }
        }
        else if(i->deadline_duration != std::chrono::steady_clock::time_point())
        {
          auto it = _durations.insert({i->deadline_duration, i});
          if(it == _durations.begin())
          {
            need_to_wake_all = true;
          }
        }
        i->is_added_to_deadline_list = true;
      }

      // Process timed out pending operations, shortening the requested timeout if necessary
      const auto durations_now = _durations.empty() ? std::chrono::steady_clock::time_point() : std::chrono::steady_clock::now();
      const auto absolutes_now = _absolutes.empty() ? std::chrono::system_clock::time_point() : std::chrono::system_clock::now();
      auto durationit = _durations.begin();
      auto absoluteit = _absolutes.begin();
      while(out.size() < in.size())
      {
        int64_t togo1 = (durationit != _durations.end()) ? std::chrono::duration_cast<std::chrono::nanoseconds>(durationit->first - durations_now).count() : INT64_MAX;
        int64_t togo2 = (absoluteit != _absolutes.end()) ? std::chrono::duration_cast<std::chrono::nanoseconds>(absoluteit->first - absolutes_now).count() : INT64_MAX;
        if(togo1 > 0)
        {
          if(nullptr == timeout || togo1 / -100 > timeout->QuadPart)
          {
            timeout = &_timeout;
            timeout->QuadPart = togo1 / -100;
          }
          durationit = _durations.end();
          togo1 = INT64_MAX;
        }
        if(togo2 > 0)
        {
          if(nullptr == timeout || togo2 < -timeout->QuadPart)
          {
            timeout = &_timeout;
            *timeout = windows_nt_kernel::from_timepoint(absoluteit->first);
          }
          absoluteit = _absolutes.end();
          togo2 = INT64_MAX;
        }
        if(durationit != _durations.end() || absoluteit != _absolutes.end())
        {
          // Choose whichever is the earliest
          if(togo1 < togo2)
          {
            out = {out.data(), out.size() + 1};
            out[out.size() - 1] = durationit->second;
            ++durationit;
          }
          else
          {
            out = {out.data(), out.size() + 1};
            out[out.size() - 1] = absoluteit->second;
            ++absoluteit;
          }
        }
      }
      return out;
    }
    catch(...)
    {
      return error_from_exception();
    }
  }

  virtual result<int> timeout_io(int max_items = -1, deadline d = deadline()) noexcept override final
  {
    LLFIO_LOG_FUNCTION_CALL(this);
    if(max_items < 0)
    {
      max_items = INT_MAX;
    }
    _lock_guard g(this->_lock);
    LARGE_INTEGER _timeout{}, *timeout = nullptr;
    bool need_to_wake_all = false;
    detail::io_operation_connection *in[64];
    OUTCOME_TRY(out, _do_timeout_io(_timeout, timeout, need_to_wake_all, {(detail::io_operation_connection **) in, (size_t) std::min(64, max_items)}));
    if(out.empty())
    {
      auto total = _durations.size() + _absolutes.size();
      return -(int) total;
    }
    g.unlock();
    LLFIO_DEADLINE_TO_SLEEP_INIT(d);
    int count = 0;
    for(auto *i : out)
    {
      i->poll();
      ++count;
      if(max_items == count)
      {
        break;
      }
      if(d)
      {
        if((d.steady && (d.nsecs == 0 || std::chrono::steady_clock::now() >= began_steady)) || (!d.steady && std::chrono::system_clock::now() >= end_utc))
        {
          break;
        }
      }
    }
    return count;
  }

  result<int> _do_complete_io(LARGE_INTEGER *timeout, int max_items) noexcept
  {
    windows_nt_kernel::init();
    using namespace windows_nt_kernel;
    if(_total_pending_io.load(std::memory_order_acquire) == 0)
    {
      return 0;
    }

    OVERLAPPED_ENTRY entries[64];
    ULONG filled = 0;
    if(max_items < 0)
    {
      max_items = INT_MAX;
    }
    if(max_items > sizeof(entries) / sizeof(entries[0]))
    {
      max_items = (int) (sizeof(entries) / sizeof(entries[0]));
    }
    NTSTATUS ntstat = NtRemoveIoCompletionEx(this->_v.h, entries, (unsigned) max_items, &filled, timeout, false);
    if(ntstat < 0 && ntstat != STATUS_TIMEOUT)
    {
      return ntkernel_error(ntstat);
    }
    if(filled == 0 || ntstat == STATUS_TIMEOUT)
    {
      return -(int) _total_pending_io.load(std::memory_order_acquire);
    }
    int count = 0;
    for(ULONG n = 0; n < filled; n++)
    {
      // If it's null, this is a post() wakeup
      if(entries[n].lpCompletionKey == 0)
      {
        continue;
      }
      auto *states = (std::vector<detail::io_operation_connection *> *) entries[n].lpCompletionKey;
      (void) states;
      // Complete the i/o
      auto *op = (typename detail::io_operation_connection *) entries[n].lpOverlapped->hEvent;
      if(!op->is_cancelled_io)
      {
        op->poll();
        ++count;
      }
      else
      {
        op->is_cancelled_io = false;
      }
    }
    if(count == 0)
    {
      return -(int) _total_pending_io.load(std::memory_order_acquire);
    }
    return count;
  }

  virtual result<int> complete_io(int max_items = -1, deadline /*unused*/ = deadline()) noexcept override final
  {
    LLFIO_LOG_FUNCTION_CALL(this);
    LARGE_INTEGER timeout;
    memset(&timeout, 0, sizeof(timeout));  // poll don't block
    return _do_complete_io(&timeout, max_items);
  }

  result<int> run(int max_items = -1, deadline d = deadline()) noexcept override final
  {
    LLFIO_LOG_FUNCTION_CALL(this);
    if(max_items < 0)
    {
      max_items = INT_MAX;
    }
    int count = 0;
    LLFIO_WIN_DEADLINE_TO_SLEEP_INIT(d);
    for(;;)
    {
      count += this->_execute_posted_items(max_items, d);
      if(max_items == count)
      {
        return count;
      }
      if(count==0 && _total_pending_io.load(std::memory_order_acquire) == 0)
      {
        return 0;  // there is nothing to block on
      }
      LLFIO_WIN_DEADLINE_TO_TIMEOUT_LOOP(d);

      // Figure out how long we can sleep the thread for
      LARGE_INTEGER _timeout{}, *timeout = nullptr;
      LLFIO_WIN_DEADLINE_TO_SLEEP_LOOP(d);  // recalculate our timeout
      bool need_to_wake_all = false;
      detail::io_operation_connection *in[64];
      _lock_guard g(this->_lock);
      // Indicate to any concurrent run() that we are about to calculate timeouts,
      // this will decrement on exit
      _concurrent_run_instances.fetch_add(1, std::memory_order_acq_rel);
      auto un_concurrent_run_instances = undoer([this] { _concurrent_run_instances.fetch_sub(1, std::memory_order_acq_rel); });
      OUTCOME_TRY(out, _do_timeout_io(_timeout, timeout, need_to_wake_all, {(detail::io_operation_connection **) in, (size_t) std::min(64, max_items - count)}));
      if(need_to_wake_all)
      {
        // Timeouts ought to be processed by all idle threads concurrently, so wake everything
        auto threads_sleeping = _concurrent_run_instances.load(std::memory_order_acquire);
        for(size_t n = 0; n < threads_sleeping; n++)
        {
          PostQueuedCompletionStatus(this->_v.h, 0, 0, nullptr);
        }
      }
      g.unlock();
      if(!out.empty())
      {
        for(auto *i : out)
        {
          i->poll();
          ++count;
          if(max_items == count)
          {
            return count;
          }
          LLFIO_DEADLINE_TO_TIMEOUT_LOOP(d);
        }
        // No need to adjust timeout after executing completions as
        // we zero the timeout below anyway
      }

      // timeout will be the lesser of the next pending i/o to expire,
      // or the deadline passed into us. If we've done any work at all,
      // only poll for i/o completions so we return immediately after.
      if(count > 0)
      {
        timeout->QuadPart = 0;
      }
      OUTCOME_TRY(items, _do_complete_io(timeout, max_items - count));
      count += items;
      if(count > 0)
      {
        return count;
      }
      // Loop if no work done, as either there are new posted items or
      // we have timed out
    }
  }

#if 0
  virtual result<int> _do_run(bool nonblocking, deadline d) noexcept override final
  {
    windows_nt_kernel::init();
    using namespace windows_nt_kernel;
    LLFIO_LOG_FUNCTION_CALL(this);
    LLFIO_WIN_DEADLINE_TO_SLEEP_INIT(d);
    for(;;)
    {
      if(check_posted_items())
      {
        return 1;
      }
      if(d && d.nsecs == 0 && _total_pending_io.load(std::memory_order_acquire) == 0)
      {
        return 0;  // there is nothing to block on
      }
      _lock_guard g(this->_lock);

      // First check if any timeouts have passed, complete those if they have
      bool need_to_wake_all = false;
      LLFIO_WIN_DEADLINE_TO_SLEEP_LOOP(d);  // recalculate our timeout
      OUTCOME_TRY(resume_timed_out, _do_timeout_io(_timeout, timeout, need_to_wake_all));
      if(nonblocking)
      {
        timeout = &_timeout;
        _timeout.QuadPart = 0;
      }
      if(need_to_wake_all)
      {
        // Timeouts ought to be processed by all idle threads concurrently, so wake everything
        auto threads_sleeping = _concurrent_run_instances.load(std::memory_order_acquire);
        for(size_t n = 0; n < threads_sleeping; n++)
        {
          PostQueuedCompletionStatus(this->_v.h, 0, 0, nullptr);
        }
      }
      if(resume_timed_out != nullptr)
      {
        g.unlock();
        // We need to do one more check if the i/o has completed before timing out
        resume_timed_out->poll();
        return 1;
      }

      OVERLAPPED_ENTRY entries[64];
      ULONG filled = 0;
      NTSTATUS ntstat;
      {
        _concurrent_run_instances.fetch_add(1, std::memory_order_acq_rel);
        g.unlock();
        // Use this instead of GetQueuedCompletionStatusEx() as this implements absolute timeouts
        ntstat = NtRemoveIoCompletionEx(this->_v.h, entries, sizeof(entries) / sizeof(entries[0]), &filled, timeout, false);
        _concurrent_run_instances.fetch_sub(1, std::memory_order_acq_rel);
      }
      if(STATUS_TIMEOUT == ntstat)
      {
        if(nonblocking)
        {
          return -(int) _total_pending_io.load(std::memory_order_acquire);
        }
        // If the supplied deadline has passed, return errc::timed_out
        LLFIO_WIN_DEADLINE_TO_TIMEOUT_LOOP(d);
        continue;
      }
      if(ntstat < 0)
      {
        return ntkernel_error(ntstat);
      }
      size_t post_wakeups = 0;
      for(ULONG n = 0; n < filled; n++)
      {
        // If it's null, this is a post() wakeup
        if(entries[n].lpCompletionKey == 0)
        {
          ++post_wakeups;
          continue;
        }
        auto *states = (std::vector<detail::io_operation_connection *> *) entries[n].lpCompletionKey;
        (void) states;
        // Complete the i/o
        auto *op = (typename detail::io_operation_connection *) entries[n].lpOverlapped->hEvent;
        if(!op->is_cancelled_io)
        {
          op->poll();
        }
        else
        {
          op->is_cancelled_io = false;
        }
      }
      if(filled - post_wakeups > 0)
      {
        return (int) (filled - post_wakeups);
      }
    }
  }
#endif
  virtual void _register_pending_io(detail::io_operation_connection *op) noexcept
  {
    op->is_registered_with_io_multiplexer = true;
    _total_pending_io.fetch_add(1, std::memory_order_relaxed);
    // Add this state to the list of pending i/o if and only if it has a deadline
    bool need_to_wake = false;
    if(op->deadline_absolute != std::chrono::system_clock::time_point() || op->deadline_duration != std::chrono::steady_clock::time_point())
    {
      _lock_guard g(this->_lock);
      op->next = nullptr;
      op->prev = _pending_end;
      if(_pending_end == nullptr)
      {
        _pending_begin = op;
      }
      else
      {
        _pending_end->next = op;
      }
      _pending_end = op;
      if(_concurrent_run_instances.load(std::memory_order_acquire) > 0)
      {
        need_to_wake = true;
      }
    }
    /* If there are run() instances running right now, wake any one of them
    to recalculate timeouts.
    */
    if(need_to_wake)
    {
      PostQueuedCompletionStatus(this->_v.h, 0, 0, nullptr);
    }
  }
  virtual void _deregister_pending_io(detail::io_operation_connection *op) noexcept
  {
    // If the i/o was cancelled, there may be an IOCP cancellation
    // packet queued. This needs to be drained before we can tear
    // down the OVERLAPPED state, otherwise memory corruption will
    // occur.
    while(op->is_cancelled_io && complete_io().value() > 0)
      ;
    _total_pending_io.fetch_sub(1, std::memory_order_relaxed);
    if(op->is_added_to_deadline_list)
    {
      _lock_guard g(this->_lock);
      if(op->deadline_absolute != std::chrono::system_clock::time_point())
      {
        auto it = _absolutes.find(op->deadline_absolute);
        if(it == _absolutes.end())
        {
          abort();
        }
        bool found = false;
        do
        {
          if(it->second == op)
          {
            _absolutes.erase(it);
            found = true;
            break;
          }
          ++it;
        } while(it != _absolutes.end() && it->first == op->deadline_absolute);
        if(!found)
        {
          abort();
        }
      }
      else if(op->deadline_duration != std::chrono::steady_clock::time_point())
      {
        auto it = _durations.find(op->deadline_duration);
        if(it == _durations.end())
        {
          abort();
        }
        bool found = false;
        do
        {
          if(it->second == op)
          {
            _durations.erase(it);
            found = true;
            break;
          }
          ++it;
        } while(it != _durations.end() && it->first == op->deadline_duration);
        if(!found)
        {
          abort();
        }
      }
      if(op->prev == nullptr)
      {
        _pending_begin = op->next;
      }
      else
      {
        op->prev->next = op->next;
      }
      if(op->next == nullptr)
      {
        _pending_end = op->prev;
      }
      else
      {
        op->next->prev = op->prev;
      }
    }
  }
};

LLFIO_HEADERS_ONLY_MEMFUNC_SPEC result<std::unique_ptr<io_multiplexer>> io_multiplexer::win_iocp(size_t threads) noexcept
{
  try
  {
    if(threads > 1)
    {
      auto ret = std::make_unique<win_iocp_impl<true>>();
      OUTCOME_TRY(ret->init(threads));
      return ret;
    }
    else
    {
      auto ret = std::make_unique<win_iocp_impl<false>>();
      OUTCOME_TRY(ret->init(1));
      return ret;
    }
  }
  catch(...)
  {
    return error_from_exception();
  }
}

LLFIO_V2_NAMESPACE_END
