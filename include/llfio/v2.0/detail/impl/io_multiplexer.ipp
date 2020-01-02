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

#include "../../io_multiplexer.hpp"

#include <mutex>

LLFIO_V2_NAMESPACE_BEGIN

namespace this_thread
{
  static LLFIO_THREAD_LOCAL io_multiplexer *_thread_multiplexer;
  LLFIO_HEADERS_ONLY_FUNC_SPEC io_multiplexer *multiplexer() noexcept
  {
    if(_thread_multiplexer == nullptr)
    {
      static auto _ = io_multiplexer::best_available(1).value();
      _thread_multiplexer = _.get();
    }
    return _thread_multiplexer;
  }
  LLFIO_HEADERS_ONLY_FUNC_SPEC void set_multiplexer(io_multiplexer *ctx) noexcept { _thread_multiplexer = ctx; }

  static LLFIO_THREAD_LOCAL struct delay_invoking_io_completion_state
  {
    detail::io_operation_connection *begin{nullptr}, *end{nullptr};
    delay_invoking_io_completion *current_raii{nullptr};
    int *count{nullptr}, nesting{0};

    void drain()
    {
      detail::io_operation_connection *_begin = begin;
      begin = nullptr;
      end = nullptr;
      int c = 0;
      while(_begin != nullptr)
      {
        auto *op = _begin;
        _begin = _begin->delay_invoking_next;
        op->_complete_io(result<size_t>(0));
        op->delay_invoking_next = op->delay_invoking_prev = nullptr;
        ++c;
      }
      *count += c;
    }
  } _delay_invoking_io_completion_state;
  void delay_invoking_io_completion::add(detail::io_operation_connection *op)
  {
    if(_delay_invoking_io_completion_state.nesting == 0)
    {
      op->_complete_io(result<size_t>(0));
      return;
    }
    if(op->delay_invoking_next != nullptr || op->delay_invoking_prev != nullptr)
    {
      abort();
    }
#ifdef LLFIO_DEBUG_PRINT
    std::cerr << "delay_invoking_io_completion::add " << op << std::endl;
#endif
    if(_delay_invoking_io_completion_state.begin == nullptr)
    {
      _delay_invoking_io_completion_state.begin = op;
      _delay_invoking_io_completion_state.end = op;
      return;
    }
    _delay_invoking_io_completion_state.end->delay_invoking_next = op;
    op->delay_invoking_prev = _delay_invoking_io_completion_state.end;
    _delay_invoking_io_completion_state.end = op;
  }
  void delay_invoking_io_completion::remove(detail::io_operation_connection *op)
  {
    if(op->delay_invoking_next == nullptr && op->delay_invoking_prev == nullptr && _delay_invoking_io_completion_state.begin!=op)
    {
      return;
    }
#ifdef LLFIO_DEBUG_PRINT
    std::cerr << "delay_invoking_io_completion::remove " << op << std::endl;
#endif
    if(op->delay_invoking_next == nullptr)
    {
      _delay_invoking_io_completion_state.end = op->delay_invoking_prev;
    }
    else
    {
      op->delay_invoking_next->delay_invoking_prev = op->delay_invoking_prev;
    }
    if(op->delay_invoking_prev == nullptr)
    {
      _delay_invoking_io_completion_state.begin = op->delay_invoking_next;
    }
    else
    {
      op->delay_invoking_prev->delay_invoking_next = op->delay_invoking_next;
    }
    op->delay_invoking_next = op->delay_invoking_prev = nullptr;
  }
  delay_invoking_io_completion::delay_invoking_io_completion(int &count)
      : _count(count)
      , _prev(_delay_invoking_io_completion_state.current_raii)
  {
    _delay_invoking_io_completion_state.count = &count;
    if(++_delay_invoking_io_completion_state.nesting == 1)
    {
      //_delay_invoking_io_completion_state.drain();
    }
#ifdef LLFIO_DEBUG_PRINT
    std::cerr << "delay_invoking_io_completion nesting = " << _delay_invoking_io_completion_state.nesting << std::endl;
#endif
  }
  delay_invoking_io_completion::~delay_invoking_io_completion()
  {
    if(_delay_invoking_io_completion_state.nesting == 1)
    {
#ifdef LLFIO_DEBUG_PRINT
      std::cerr << "~delay_invoking_io_completion drain" << std::endl;
#endif
      _delay_invoking_io_completion_state.drain();
    }
    --_delay_invoking_io_completion_state.nesting;
    _delay_invoking_io_completion_state.current_raii = _prev;
    _delay_invoking_io_completion_state.count = (_prev != nullptr) ? &_prev->_count : nullptr;
#ifdef LLFIO_DEBUG_PRINT
    std::cerr << "~delay_invoking_io_completion nesting = " << _delay_invoking_io_completion_state.nesting << std::endl;
#endif
  }
}  // namespace this_thread

template <bool threadsafe> class io_multiplexer_impl : public io_multiplexer
{
  struct _fake_lock_guard
  {
    explicit _fake_lock_guard(std::mutex & /*unused*/) {}
    void lock() {}
    void unlock() {}
  };

protected:
  using _lock_guard = std::conditional_t<threadsafe, std::unique_lock<std::mutex>, _fake_lock_guard>;
  std::mutex _lock;
  std::atomic<bool> _nonzero_items_posted{false};
  function_ptr<void *(void *)> _items_posted, *_last_item_posted{nullptr};

  int _execute_posted_items(int max_items, deadline d)
  {
    if(_nonzero_items_posted.load(std::memory_order_acquire))
    {
      LLFIO_DEADLINE_TO_SLEEP_INIT(d);
      if(max_items < 0)
      {
        max_items = INT_MAX;
      }
      function_ptr<void *(void *)> i, remaining, empty, *remaining_last_item_posted;
      {
        std::lock_guard<std::mutex> h(_lock);  // need real locking here
        remaining = std::move(_items_posted);
        remaining_last_item_posted = _last_item_posted;
        _last_item_posted = nullptr;
        _nonzero_items_posted.store(false, std::memory_order_release);
      }
      int count = 0;
      do
      {
        // Execute the item
        i = std::move(remaining);
        remaining = std::move(*reinterpret_cast<function_ptr<void *(void *)> *>(i(&empty)));
        i(nullptr);
        ++count;
        if(!remaining)
        {
          break;
        }
        if(d)
        {
          if((d.steady && (d.nsecs == 0 || std::chrono::steady_clock::now() >= began_steady)) || (!d.steady && std::chrono::system_clock::now() >= end_utc))
          {
            // Reinsert remaining into list
            std::lock_guard<std::mutex> h(_lock);  // need real locking here
            if(_last_item_posted == nullptr)
            {
              _items_posted = std::move(remaining);
              _last_item_posted = remaining_last_item_posted;
            }
            else
            {
              // Place new list at tail of remaining
              (*remaining_last_item_posted)(&_items_posted);
              _items_posted = std::move(remaining);
            }
            _nonzero_items_posted.store(true, std::memory_order_release);
            return count;
          }
        }
      } while(count < max_items);
      return count;
    }
    return 0;
  }

public:
  // Lock should be held on entry!
  virtual ~io_multiplexer_impl()
  {
    if(_nonzero_items_posted)
    {
      function_ptr<void *(void *)> empty;
      while(_items_posted)
      {
        auto &next = *reinterpret_cast<function_ptr<void *(void *)> *>(_items_posted(&empty));
        _items_posted = std::move(next);
      }
    }
    _lock.unlock();
  }

  virtual void _post(function_ptr<void *(void *)> &&f) noexcept override
  {
    std::lock_guard<std::mutex> h(_lock);  // need real locking here
    if(_last_item_posted == nullptr)
    {
      _items_posted = std::move(f);
      _last_item_posted = &_items_posted;
      _nonzero_items_posted.store(true, std::memory_order_release);
      return;
    }
    // Store the new item into the most recently posted item
    _last_item_posted = reinterpret_cast<function_ptr<void *(void *)> *>((*_last_item_posted)(&f));
  }
};

LLFIO_HEADERS_ONLY_MEMFUNC_SPEC result<std::unique_ptr<io_multiplexer>> io_multiplexer::best_available(size_t threads) noexcept
{
#ifdef __linux__
  if(threads > 1)
  {
    return io_multiplexer::linux_epoll(threads);
  }
  auto r = io_multiplexer::linux_io_uring();
  if(r)
  {
    return r;
  }
  return io_multiplexer::linux_epoll(threads);
#elif defined(__FreeBSD__) || defined(__APPLE__)
  return io_multiplexer::bsd_kqueue(threads);
#elif defined(_WIN32)
  if(threads > 1)
  {
    return io_multiplexer::win_iocp(threads);
  }
  return io_multiplexer::win_alertable();
#else
#error Unknown platform
  return errc::not_supported;
#endif
}

LLFIO_V2_NAMESPACE_END

#if defined(_WIN32)
#include "windows/io_multiplexer.ipp"
#else
#include "posix/io_multiplexer.ipp"
#endif
