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

#ifndef LLFIO_IO_MULTIPLEXER_H
#define LLFIO_IO_MULTIPLEXER_H

//#define LLFIO_DEBUG_PRINT

#include "handle.hpp"

#include <memory>  // for unique_ptr and shared_ptr

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4251)  // dll interface
#endif

LLFIO_V2_NAMESPACE_EXPORT_BEGIN

class io_handle;

/*! \class io_multiplexer
\brief A multiplexer of byte-orientated i/o.

LLFIO does not provide out-of-the-box multiplexing of byte i/o, however it does provide the ability
to create `io_handle` instances with the `handle::flag::multiplexable` set. With that flag set, the
following LLFIO classes change how they create handles with the kernel:

<table>
<tr><th>LLFIO i/o class<th>POSIX<th>Windows
<tr><td><code>directory_handle</code><td>No effect<td>Creates `HANDLE` as `OVERLAPPED`
<tr><td><code>file_handle</code><td>No effect<td>Creates `HANDLE` as `OVERLAPPED`
<tr><td><code>map_handle</code><td>No effect<td>No effect
<tr><td><code>mapped_file_handle</code><td>No effect<td>Creates `HANDLE` as `OVERLAPPED`, but i/o is to map not file
<tr><td><code>pipe_handle</code><td>Creates file descriptor as non-blocking<td>Creates `HANDLE` as `OVERLAPPED`
<tr><td><code>section_handle</code><td>No effect<td>Creates `HANDLE` as `OVERLAPPED`
<tr><td><code>symlink_handle</code><td>No effect<td>Creates `HANDLE` as `OVERLAPPED`
</table>

If the i/o handle's multiplexer pointer is not null, the multiplexer instance is invoked to implement
`io_handle::read()`, `io_handle::write()` and `io_handle::barrier()`. You can define in your
multiplexer implementation the byte i/o read, write and barrier implementations to anything you like,
though you should not break the behaviour guarantees documented for those operations.

If the i/o handle's multiplexer pointer is null, `io_handle::read()`, `io_handle::write()` and
`io_handle::barrier()` all use virtually overridable implementations. The default implementations
emulate blocking semantics using the kernel's i/o poll function (literally `poll()` on POSIX,
`NtWaitForSingleObject()` on Windows) to sleep the thread until at least one byte of i/o occurs, or
the deadline specified is exceeded. This, obviously enough, can double the number of kernel syscalls
done per i/o, so using handles with the `handle::flag::multiplexable` flag set is not wise unless
you really need non-infinite deadline i/o.
*/
class LLFIO_DECL io_multiplexer : public handle
{
  friend class io_handle;

  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC result<int> _register_io_handle(io_handle *h) noexcept = 0;
  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC result<void> _deregister_io_handle(io_handle *h) noexcept = 0;

public:
  using path_type = handle::path_type;
  using extent_type = handle::extent_type;
  using size_type = handle::size_type;
  using mode = handle::mode;
  using creation = handle::creation;
  using caching = handle::caching;
  using flag = handle::flag;

  //! The kinds of write reordering barrier which can be performed.
  enum class barrier_kind : uint8_t
  {
    nowait_data_only,  //!< Barrier data only, non-blocking. This is highly optimised on NV-DIMM storage, but consider using `nvram_barrier()` for even better performance.
    wait_data_only,    //!< Barrier data only, block until it is done. This is highly optimised on NV-DIMM storage, but consider using `nvram_barrier()` for even better performance.
    nowait_all,        //!< Barrier data and the metadata to retrieve it, non-blocking.
    wait_all           //!< Barrier data and the metadata to retrieve it, block until it is done.
  };

  //! The scatter buffer type used by this handle. Guaranteed to be `TrivialType` and `StandardLayoutType`.
  //! Try to make address and length 64 byte, or ideally, `page_size()` aligned where possible.
  struct buffer_type
  {
    //! Type of the pointer to memory.
    using pointer = byte *;
    //! Type of the pointer to memory.
    using const_pointer = const byte *;
    //! Type of the iterator to memory.
    using iterator = byte *;
    //! Type of the iterator to memory.
    using const_iterator = const byte *;
    //! Type of the length of memory.
    using size_type = size_t;

    //! Default constructor
    buffer_type() = default;
    //! Constructor
    constexpr buffer_type(pointer data, size_type len) noexcept
        : _data(data)
        , _len(len)
    {
    }
    //! Constructor
    constexpr buffer_type(span<byte> s) noexcept
        : _data(s.data())
        , _len(s.size())
    {
    }
    buffer_type(const buffer_type &) = default;
    buffer_type(buffer_type &&) = default;
    buffer_type &operator=(const buffer_type &) = default;
    buffer_type &operator=(buffer_type &&) = default;
    ~buffer_type() = default;

    // Emulation of this being a span<byte> in the TS

    //! Returns the address of the bytes for this buffer
    constexpr pointer data() noexcept { return _data; }
    //! Returns the address of the bytes for this buffer
    constexpr const_pointer data() const noexcept { return _data; }
    //! Returns the number of bytes in this buffer
    constexpr size_type size() const noexcept { return _len; }

    //! Returns an iterator to the beginning of the buffer
    constexpr iterator begin() noexcept { return _data; }
    //! Returns an iterator to the beginning of the buffer
    constexpr const_iterator begin() const noexcept { return _data; }
    //! Returns an iterator to the beginning of the buffer
    constexpr const_iterator cbegin() const noexcept { return _data; }
    //! Returns an iterator to after the end of the buffer
    constexpr iterator end() noexcept { return _data + _len; }
    //! Returns an iterator to after the end of the buffer
    constexpr const_iterator end() const noexcept { return _data + _len; }
    //! Returns an iterator to after the end of the buffer
    constexpr const_iterator cend() const noexcept { return _data + _len; }

  private:
    friend constexpr inline void _check_iovec_match();
    pointer _data;
    size_type _len;
  };
  //! The registered scatter buffer type used by this handle.
  using registered_buffer_type = std::shared_ptr<buffer_type>;

  //! The gather buffer type used by this handle. Guaranteed to be `TrivialType` and `StandardLayoutType`.
  //! Try to make address and length 64 byte, or ideally, `page_size()` aligned where possible.
  struct const_buffer_type
  {
    //! Type of the pointer to memory.
    using pointer = const byte *;
    //! Type of the pointer to memory.
    using const_pointer = const byte *;
    //! Type of the iterator to memory.
    using iterator = const byte *;
    //! Type of the iterator to memory.
    using const_iterator = const byte *;
    //! Type of the length of memory.
    using size_type = size_t;

    //! Default constructor
    const_buffer_type() = default;
    //! Constructor
    constexpr const_buffer_type(pointer data, size_type len) noexcept
        : _data(data)
        , _len(len)
    {
    }
    //! Constructor
    constexpr const_buffer_type(span<const byte> s) noexcept
        : _data(s.data())
        , _len(s.size())
    {
    }
    //! Converting constructor from non-const buffer type
    constexpr const_buffer_type(buffer_type b) noexcept
        : _data(b.data())
        , _len(b.size())
    {
    }
    //! Converting constructor from non-const buffer type
    constexpr const_buffer_type(span<byte> s) noexcept
        : _data(s.data())
        , _len(s.size())
    {
    }
    const_buffer_type(const const_buffer_type &) = default;
    const_buffer_type(const_buffer_type &&) = default;
    const_buffer_type &operator=(const const_buffer_type &) = default;
    const_buffer_type &operator=(const_buffer_type &&) = default;
    ~const_buffer_type() = default;

    // Emulation of this being a span<byte> in the TS

    //! Returns the address of the bytes for this buffer
    constexpr pointer data() noexcept { return _data; }
    //! Returns the address of the bytes for this buffer
    constexpr const_pointer data() const noexcept { return _data; }
    //! Returns the number of bytes in this buffer
    constexpr size_type size() const noexcept { return _len; }

    //! Returns an iterator to the beginning of the buffer
    constexpr iterator begin() noexcept { return _data; }
    //! Returns an iterator to the beginning of the buffer
    constexpr const_iterator begin() const noexcept { return _data; }
    //! Returns an iterator to the beginning of the buffer
    constexpr const_iterator cbegin() const noexcept { return _data; }
    //! Returns an iterator to after the end of the buffer
    constexpr iterator end() noexcept { return _data + _len; }
    //! Returns an iterator to after the end of the buffer
    constexpr const_iterator end() const noexcept { return _data + _len; }
    //! Returns an iterator to after the end of the buffer
    constexpr const_iterator cend() const noexcept { return _data + _len; }

  private:
    pointer _data;
    size_type _len;
  };
#ifndef NDEBUG
  static_assert(std::is_trivial<buffer_type>::value, "buffer_type is not a trivial type!");
  static_assert(std::is_trivial<const_buffer_type>::value, "const_buffer_type is not a trivial type!");
  static_assert(std::is_standard_layout<buffer_type>::value, "buffer_type is not a standard layout type!");
  static_assert(std::is_standard_layout<const_buffer_type>::value, "const_buffer_type is not a standard layout type!");
#endif
  //! The registered gather buffer type used by this handle.
  using registered_const_buffer_type = std::shared_ptr<const_buffer_type>;

  //! The scatter buffers type used by this handle. Guaranteed to be `TrivialType` apart from construction, and `StandardLayoutType`.
  using buffers_type = span<buffer_type>;
  //! The gather buffers type used by this handle. Guaranteed to be `TrivialType` apart from construction, and `StandardLayoutType`.
  using const_buffers_type = span<const_buffer_type>;
#ifndef NDEBUG
  // Is trivial in all ways, except default constructibility
  static_assert(std::is_trivially_copyable<buffers_type>::value, "buffers_type is not trivially copyable!");
  // static_assert(std::is_trivially_assignable<buffers_type, buffers_type>::value, "buffers_type is not trivially assignable!");
  // static_assert(std::is_trivially_destructible<buffers_type>::value, "buffers_type is not trivially destructible!");
  // static_assert(std::is_trivially_copy_constructible<buffers_type>::value, "buffers_type is not trivially copy constructible!");
  // static_assert(std::is_trivially_move_constructible<buffers_type>::value, "buffers_type is not trivially move constructible!");
  // static_assert(std::is_trivially_copy_assignable<buffers_type>::value, "buffers_type is not trivially copy assignable!");
  // static_assert(std::is_trivially_move_assignable<buffers_type>::value, "buffers_type is not trivially move assignable!");
  static_assert(std::is_standard_layout<buffers_type>::value, "buffers_type is not a standard layout type!");
#endif
  //! The registered scatter buffers type used by this handle. Guaranteed to be `TrivialType` apart from construction, and `StandardLayoutType`.
  using registered_buffers_type = span<registered_buffer_type *>;
  //! The registered gather buffers type used by this handle. Guaranteed to be `TrivialType` apart from construction, and `StandardLayoutType`.
  using registered_const_buffers_type = span<registered_const_buffer_type *>;

  //! The i/o request type used by this handle. Guaranteed to be `TrivialType` apart from construction, and `StandardLayoutType`.
  template <class T> struct io_request
  {
    T buffers{};
    extent_type offset{0};
    constexpr io_request() {}  // NOLINT (defaulting this breaks clang and GCC, so don't do it!)
    constexpr io_request(T _buffers, extent_type _offset)
        : buffers(std::move(_buffers))
        , offset(_offset)
    {
    }
  };
#ifndef NDEBUG
  // Is trivial in all ways, except default constructibility
  static_assert(std::is_trivially_copyable<io_request<buffers_type>>::value, "io_request<buffers_type> is not trivially copyable!");
  // static_assert(std::is_trivially_assignable<io_request<buffers_type>, io_request<buffers_type>>::value, "io_request<buffers_type> is not trivially assignable!");
  // static_assert(std::is_trivially_destructible<io_request<buffers_type>>::value, "io_request<buffers_type> is not trivially destructible!");
  // static_assert(std::is_trivially_copy_constructible<io_request<buffers_type>>::value, "io_request<buffers_type> is not trivially copy constructible!");
  // static_assert(std::is_trivially_move_constructible<io_request<buffers_type>>::value, "io_request<buffers_type> is not trivially move constructible!");
  // static_assert(std::is_trivially_copy_assignable<io_request<buffers_type>>::value, "io_request<buffers_type> is not trivially copy assignable!");
  // static_assert(std::is_trivially_move_assignable<io_request<buffers_type>>::value, "io_request<buffers_type> is not trivially move assignable!");
  static_assert(std::is_standard_layout<io_request<buffers_type>>::value, "io_request<buffers_type> is not a standard layout type!");
#endif
  //! The i/o result type used by this handle. Guaranteed to be `TrivialType` apart from construction.
  template <class T> struct io_result : public LLFIO_V2_NAMESPACE::result<T>
  {
    using Base = LLFIO_V2_NAMESPACE::result<T>;
    size_type _bytes_transferred{static_cast<size_type>(-1)};

#if defined(_MSC_VER) && !defined(__clang__)  // workaround MSVC parsing bug
    constexpr io_result()
        : Base()
    {
    }
    template <class... Args>
    constexpr io_result(Args &&... args)
        : Base(std::forward<Args>(args)...)
    {
    }
#else
    using Base::Base;
    io_result() = default;
#endif
    ~io_result() = default;
    io_result &operator=(io_result &&) = default;  // NOLINT
#if LLFIO_EXPERIMENTAL_STATUS_CODE
    io_result(const io_result &) = delete;
    io_result &operator=(const io_result &) = delete;
#else
    io_result(const io_result &) = default;
    io_result &operator=(const io_result &) = default;
#endif
    io_result(io_result &&) = default;  // NOLINT
    //! Returns bytes transferred
    size_type bytes_transferred() noexcept
    {
      if(_bytes_transferred == static_cast<size_type>(-1))
      {
        _bytes_transferred = 0;
        for(auto &i : this->value())
        {
          _bytes_transferred += i.size();
        }
      }
      return _bytes_transferred;
    }
  };
#if !defined(NDEBUG) && !LLFIO_EXPERIMENTAL_STATUS_CODE
  // Is trivial in all ways, except default constructibility
  static_assert(std::is_trivially_copyable<io_result<buffers_type>>::value, "io_result<buffers_type> is not trivially copyable!");
// static_assert(std::is_trivially_assignable<io_result<buffers_type>, io_result<buffers_type>>::value, "io_result<buffers_type> is not trivially assignable!");
// static_assert(std::is_trivially_destructible<io_result<buffers_type>>::value, "io_result<buffers_type> is not trivially destructible!");
// static_assert(std::is_trivially_copy_constructible<io_result<buffers_type>>::value, "io_result<buffers_type> is not trivially copy constructible!");
// static_assert(std::is_trivially_move_constructible<io_result<buffers_type>>::value, "io_result<buffers_type> is not trivially move constructible!");
// static_assert(std::is_trivially_copy_assignable<io_result<buffers_type>>::value, "io_result<buffers_type> is not trivially copy assignable!");
// static_assert(std::is_trivially_move_assignable<io_result<buffers_type>>::value, "io_result<buffers_type> is not trivially move assignable!");
//! \todo Why is io_result<buffers_type> not a standard layout type?
// static_assert(std::is_standard_layout<result<buffers_type>>::value, "result<buffers_type> is not a standard layout type!");
// static_assert(std::is_standard_layout<io_result<buffers_type>>::value, "io_result<buffers_type> is not a standard layout type!");
#endif

  using handle::handle;
  io_multiplexer(io_multiplexer &&) = default;
  io_multiplexer(const io_multiplexer &) = delete;
  io_multiplexer &operator=(io_multiplexer &&) = default;
  io_multiplexer &operator=(const io_multiplexer &) = delete;
  ~io_multiplexer() = default;

public:
  //! Implements `io_handle::max_buffers()`
  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC size_t do_io_handle_max_buffers(const io_handle *h) const noexcept = 0;
  //! Implements `io_handle::allocate_registered_buffer()`
  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC result<registered_buffer_type> do_io_handle_allocate_registered_buffer(io_handle *h, size_t &bytes) noexcept = 0;
  //! Implements `io_handle::read()`
  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC io_result<buffers_type> do_io_handle_read(io_handle *h, io_request<buffers_type> reqs, deadline d) noexcept = 0;
  //! Implements `io_handle::read()`
  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC io_result<registered_buffers_type> do_io_handle_read(io_handle *h, io_request<registered_buffers_type> reqs, deadline d) noexcept = 0;
  //! Implements `io_handle::write()`
  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC io_result<const_buffers_type> do_io_handle_write(io_handle *h, io_request<const_buffers_type> reqs, deadline d) noexcept = 0;
  //! Implements `io_handle::write()`
  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC io_result<registered_const_buffers_type> do_io_handle_write(io_handle *h, io_request<registered_const_buffers_type> reqs, deadline d) noexcept = 0;
  //! Implements `io_handle::barrier()`
  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC io_result<const_buffers_type> do_io_handle_barrier(io_handle *h, io_request<const_buffers_type> reqs, barrier_kind kind, deadline d) noexcept = 0;
  //! Implements `io_handle::barrier()`
  LLFIO_HEADERS_ONLY_VIRTUAL_SPEC io_result<registered_const_buffers_type> do_io_handle_barrier(io_handle *h, io_request<registered_const_buffers_type> reqs, barrier_kind kind, deadline d) noexcept = 0;
};
//! A unique ptr to an i/o multiplexer implementation.
using io_multiplexer_ptr = std::unique_ptr<io_multiplexer>;

//! \brief Choose the best available i/o multiplexer implementation for this platform.
// LLFIO_HEADERS_ONLY_FUNC_SPEC result<io_multiplexer_ptr> multiplexer_best_available(size_t threads) noexcept;
#if defined(__linux__) || DOXYGEN_IS_IN_THE_HOUSE
// LLFIO_HEADERS_ONLY_FUNC_SPEC result<io_multiplexer_ptr> multiplexer_linux_epoll(size_t threads) noexcept;
// LLFIO_HEADERS_ONLY_FUNC_SPEC result<io_multiplexer_ptr> multiplexer_linux_io_uring() noexcept;
#endif
#if(defined(__FreeBSD__) || defined(__APPLE__)) || DOXYGEN_IS_IN_THE_HOUSE
// LLFIO_HEADERS_ONLY_FUNC_SPEC result<io_multiplexer_ptr> multiplexer_bsd_kqueue(size_t threads) noexcept;
#endif
#if defined(_WIN32) || DOXYGEN_IS_IN_THE_HOUSE
// LLFIO_HEADERS_ONLY_FUNC_SPEC result<io_multiplexer_ptr> multiplexer_win_iocp(size_t threads) noexcept;
#endif

//! \brief Thread local settings
namespace this_thread
{
  //! \brief Return the calling thread's current i/o multiplexer.
  LLFIO_HEADERS_ONLY_FUNC_SPEC io_multiplexer *multiplexer() noexcept;
  //! \brief Set the calling thread's current i/o multiplexer.
  LLFIO_HEADERS_ONLY_FUNC_SPEC void set_multiplexer(io_multiplexer *ctx) noexcept;
}  // namespace this_thread

// BEGIN make_free_functions.py
// END make_free_functions.py

LLFIO_V2_NAMESPACE_END

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#if LLFIO_HEADERS_ONLY == 1 && !defined(DOXYGEN_SHOULD_SKIP_THIS)
#define LLFIO_INCLUDED_BY_HEADER 1
#include "detail/impl/io_multiplexer.ipp"
#undef LLFIO_INCLUDED_BY_HEADER
#endif

#endif
