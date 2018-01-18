/* A handle to a source of mapped memory
(C) 2016-2017 Niall Douglas <http://www.nedproductions.biz/> (14 commits)
File Created: August 2016


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

#ifndef AFIO_MAP_HANDLE_H
#define AFIO_MAP_HANDLE_H

#include "file_handle.hpp"

//! \file map_handle.hpp Provides `map_handle`

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4251)  // dll interface
#endif

AFIO_V2_NAMESPACE_EXPORT_BEGIN

/*! \class section_handle
\brief A handle to a source of mapped memory.

There are two configurations of section handle, one where the user supplies the file backing for the
section and the other where an internal file descriptor to an unnamed inode in a tmpfs or ramfs based
temporary directory is kept and managed. The latter is merely a convenience for creating an anonymous
source of memory which can be resized whilst preserving contents: see `algorithm::trivial_vector<T>`.

On Windows the native handle of this handle is that of the NT kernel section object. On POSIX it is
a cloned file descriptor of the backing storage if there is backing storage, else it will be the
aforementioned file descriptor to an unnamed inode.
*/
class AFIO_DECL section_handle : public handle
{
public:
  using extent_type = handle::extent_type;
  using size_type = handle::size_type;

  //! The behaviour of the memory section
  QUICKCPPLIB_BITFIELD_BEGIN(flag){none = 0,          //!< No flags
                                   read = 1 << 0,     //!< Memory views can be read
                                   write = 1 << 1,    //!< Memory views can be written
                                   cow = 1 << 2,      //!< Memory views can be copy on written
                                   execute = 1 << 3,  //!< Memory views can execute code

                                   nocommit = 1 << 8,     //!< Don't allocate space for this memory in the system immediately
                                   prefault = 1 << 9,     //!< Prefault, as if by reading every page, any views of memory upon creation.
                                   executable = 1 << 10,  //!< The backing storage is in fact an executable program binary.
                                   singleton = 1 << 11,   //!< A single instance of this section is to be shared by all processes using the same backing file.

                                   barrier_on_close = 1 << 16,  //!< Maps of this section, if writable, issue a `barrier()` when destructed blocking until data (not metadata) reaches physical storage.

                                   // NOTE: IF UPDATING THIS UPDATE THE std::ostream PRINTER BELOW!!!

                                   readwrite = (read | write)};
  QUICKCPPLIB_BITFIELD_END(flag);

protected:
  file_handle *_backing{nullptr};
  file_handle _anonymous;
  flag _flag{flag::none};

public:
  AFIO_HEADERS_ONLY_VIRTUAL_SPEC ~section_handle() override;
  AFIO_HEADERS_ONLY_VIRTUAL_SPEC result<void> close() noexcept override;
  //! Default constructor
  section_handle() = default;
  //! Construct a section handle using the given native handle type for the section and the given i/o handle for the backing storage
  explicit section_handle(native_handle_type sectionh, file_handle *backing, file_handle anonymous, flag __flag)
      : handle(sectionh, handle::caching::all)
      , _backing(backing)
      , _anonymous(std::move(anonymous))
      , _flag(__flag)
  {
  }
  //! Implicit move construction of section_handle permitted
  constexpr section_handle(section_handle &&o) noexcept : handle(std::move(o)), _backing(o._backing), _anonymous(std::move(o._anonymous)), _flag(o._flag)
  {
    o._backing = nullptr;
    o._flag = flag::none;
  }
  //! Move assignment of section_handle permitted
  section_handle &operator=(section_handle &&o) noexcept
  {
    this->~section_handle();
    new(this) section_handle(std::move(o));
    return *this;
  }
  //! Swap with another instance
  AFIO_MAKE_FREE_FUNCTION
  void swap(section_handle &o) noexcept
  {
    section_handle temp(std::move(*this));
    *this = std::move(o);
    o = std::move(temp);
  }

  /*! \brief Create a memory section backed by a file.
  \param backing The handle to use as backing storage.
  \param bytes The initial size of this section, which cannot be larger than any backing file. Zero means to use `backing.length()`.
  \param _flag How to create the section.

  \errors Any of the values POSIX dup(), open() or NtCreateSection() can return.
  */
  AFIO_MAKE_FREE_FUNCTION
  static AFIO_HEADERS_ONLY_MEMFUNC_SPEC result<section_handle> section(file_handle &backing, extent_type bytes, flag _flag) noexcept;
  /*! \brief Create a memory section backed by a file.
  \param backing The handle to use as backing storage.
  \param bytes The initial size of this section, which cannot be larger than any backing file. Zero means to use `backing.length()`.

  This convenience overload create a writable section if the backing file is writable, otherwise a read-only section.

  \errors Any of the values POSIX dup(), open() or NtCreateSection() can return.
  */
  AFIO_MAKE_FREE_FUNCTION
  static result<section_handle> section(file_handle &backing, extent_type bytes = 0) noexcept { return section(backing, bytes, backing.is_writable() ? (flag::readwrite) : (flag::read)); }
  /*! \brief Create a memory section backed by an anonymous, managed file.
  \param bytes The initial size of this section. Cannot be zero.
  \param dirh Where to create the anonymous, managed file.
  \param _flag How to create the section.

  \errors Any of the values POSIX dup(), open() or NtCreateSection() can return.
  */
  AFIO_MAKE_FREE_FUNCTION
  static AFIO_HEADERS_ONLY_MEMFUNC_SPEC result<section_handle> section(extent_type bytes, const path_handle &dirh = path_discovery::storage_backed_temporary_files_directory(), flag _flag = flag::read | flag::write) noexcept;

  //! Returns the memory section's flags
  flag section_flags() const noexcept { return _flag; }
  //! Returns the borrowed handle backing this section, if any
  file_handle *backing() const noexcept { return _backing; }
  //! Sets the borrowed handle backing this section, if any
  void set_backing(file_handle *fh) noexcept { _backing = fh; }
  //! Returns the borrowed native handle backing this section
  native_handle_type backing_native_handle() const noexcept { return _backing != nullptr ? _backing->native_handle() : native_handle_type(); }
  //! Return the current maximum permitted extent of the memory section.
  AFIO_MAKE_FREE_FUNCTION
  AFIO_HEADERS_ONLY_MEMFUNC_SPEC result<extent_type> length() const noexcept;

  /*! Resize the current maximum permitted extent of the memory section to the given extent.
  \param newsize The new size of the memory section, which cannot be zero. Specify zero to use `backing.length()`.
  This cannot exceed the size of any backing file used if that file is not writable.

  \errors Any of the values `NtExtendSection()` or `ftruncate()` can return.
  */
  AFIO_MAKE_FREE_FUNCTION
  AFIO_HEADERS_ONLY_MEMFUNC_SPEC result<extent_type> truncate(extent_type newsize = 0) noexcept;
};
inline std::ostream &operator<<(std::ostream &s, const section_handle::flag &v)
{
  std::string temp;
  if(!!(v & section_handle::flag::read))
  {
    temp.append("read|");
  }
  if(!!(v & section_handle::flag::write))
  {
    temp.append("write|");
  }
  if(!!(v & section_handle::flag::cow))
  {
    temp.append("cow|");
  }
  if(!!(v & section_handle::flag::execute))
  {
    temp.append("execute|");
  }
  if(!!(v & section_handle::flag::nocommit))
  {
    temp.append("nocommit|");
  }
  if(!!(v & section_handle::flag::prefault))
  {
    temp.append("prefault|");
  }
  if(!!(v & section_handle::flag::executable))
  {
    temp.append("executable|");
  }
  if(!!(v & section_handle::flag::singleton))
  {
    temp.append("singleton|");
  }
  if(!!(v & section_handle::flag::barrier_on_close))
  {
    temp.append("barrier_on_close|");
  }
  if(!temp.empty())
  {
    temp.resize(temp.size() - 1);
    if(std::count(temp.cbegin(), temp.cend(), '|') > 0)
    {
      temp = "(" + temp + ")";
    }
  }
  else
  {
    temp = "none";
  }
  return s << "afio::section_handle::flag::" << temp;
}

//! \brief Constructor for `section_handle`
template <> struct construct<section_handle>
{
  file_handle &backing;
  section_handle::extent_type maximum_size = 0;
  section_handle::flag _flag = section_handle::flag::read | section_handle::flag::write;
  result<section_handle> operator()() const noexcept { return section_handle::section(backing, maximum_size, _flag); }
};

class mapped_file_handle;

/*! \class map_handle
\brief A handle to a memory mapped region of memory.

\note The native handle returned by this map handle is always that of the backing storage, but closing this handle
does not close that of the backing storage, nor does releasing this handle release that of the backing storage.
Locking byte ranges of this handle is therefore equal to locking byte ranges in the original backing storage.

\sa `mapped_file_handle`, `algorithm::mapped_view`
*/
class AFIO_DECL map_handle : public io_handle
{
  friend class mapped_file_handle;

public:
  using extent_type = io_handle::extent_type;
  using size_type = io_handle::size_type;
  using mode = io_handle::mode;
  using creation = io_handle::creation;
  using caching = io_handle::caching;
  using flag = io_handle::flag;
  using buffer_type = io_handle::buffer_type;
  using const_buffer_type = io_handle::const_buffer_type;
  using buffers_type = io_handle::buffers_type;
  using const_buffers_type = io_handle::const_buffers_type;
  template <class T> using io_request = io_handle::io_request<T>;
  template <class T> using io_result = io_handle::io_result<T>;

protected:
  section_handle *_section{nullptr};
  char *_addr{nullptr};
  extent_type _offset{0};
  size_type _length{0};
  section_handle::flag _flag{section_handle::flag::none};

  explicit map_handle(section_handle *section)
      : _section(section)
      , _flag(section != nullptr ? section->section_flags() : section_handle::flag::none)
  {
  }

public:
  //! Default constructor
  constexpr map_handle() {}
  AFIO_HEADERS_ONLY_VIRTUAL_SPEC ~map_handle() override;
  //! Implicit move construction of map_handle permitted
  constexpr map_handle(map_handle &&o) noexcept : io_handle(std::move(o)), _section(o._section), _addr(o._addr), _offset(o._offset), _length(o._length), _flag(o._flag)
  {
    o._section = nullptr;
    o._addr = nullptr;
    o._offset = 0;
    o._length = 0;
    o._flag = section_handle::flag::none;
  }
  //! Move assignment of map_handle permitted
  map_handle &operator=(map_handle &&o) noexcept
  {
    this->~map_handle();
    new(this) map_handle(std::move(o));
    return *this;
  }
  //! Swap with another instance
  AFIO_MAKE_FREE_FUNCTION
  void swap(map_handle &o) noexcept
  {
    map_handle temp(std::move(*this));
    *this = std::move(o);
    o = std::move(temp);
  }

  //! Unmap the mapped view.
  AFIO_MAKE_FREE_FUNCTION
  AFIO_HEADERS_ONLY_VIRTUAL_SPEC result<void> close() noexcept override;
  //! Releases the mapped view, but does NOT release the native handle.
  AFIO_HEADERS_ONLY_VIRTUAL_SPEC native_handle_type release() noexcept override;
  AFIO_MAKE_FREE_FUNCTION
  AFIO_HEADERS_ONLY_VIRTUAL_SPEC io_result<const_buffers_type> barrier(io_request<const_buffers_type> reqs = io_request<const_buffers_type>(), bool wait_for_device = false, bool and_metadata = false, deadline d = deadline()) noexcept override;


  /*! Create new memory and map it into view.
  \param bytes How many bytes to create and map. Typically will be rounded to a multiple of the page size (see utils::page_sizes()).
  \param _flag The permissions with which to map the view which are constrained by the permissions of the memory section. `flag::none` can be useful for reserving virtual address space without committing system resources, use commit() to later change availability of memory.

  \note On Microsoft Windows this constructor uses the faster VirtualAlloc() which creates less versatile page backed memory. If you want anonymous memory
  allocated from a paging file backed section instead, create a page file backed section and then a mapped view from that using
  the other constructor. This makes available all those very useful VM tricks Windows can do with section mapped memory which
  VirtualAlloc() memory cannot do.

  \errors Any of the values POSIX mmap() or VirtualAlloc() can return.
  */
  AFIO_MAKE_FREE_FUNCTION
  static AFIO_HEADERS_ONLY_MEMFUNC_SPEC result<map_handle> map(size_type bytes, section_handle::flag _flag = section_handle::flag::readwrite) noexcept;

  /*! Create a memory mapped view of a backing storage.
  \param section A memory section handle specifying the backing storage to use.
  \param bytes How many bytes to map (0 = the size of the memory section).
  \param offset The offset into the backing storage to map from. Typically needs to be at least a multiple of the page size (see utils::page_sizes()), on Windows it needs to be a multiple of the kernel memory allocation granularity (typically 64Kb).
  \param _flag The permissions with which to map the view which are constrained by the permissions of the memory section. `flag::none` can be useful for reserving virtual address space without committing system resources, use commit() to later change availability of memory.

  \errors Any of the values POSIX mmap() or NtMapViewOfSection() can return.
  */
  AFIO_MAKE_FREE_FUNCTION
  static AFIO_HEADERS_ONLY_MEMFUNC_SPEC result<map_handle> map(section_handle &section, size_type bytes = 0, extent_type offset = 0, section_handle::flag _flag = section_handle::flag::readwrite) noexcept;

  //! The memory section this handle is using
  section_handle *section() const noexcept { return _section; }
  //! Sets the memory section this handle is using
  void set_section(section_handle *s) noexcept { _section = s; }

  //! The address in memory where this mapped view resides
  char *address() const noexcept { return _addr; }

  //! The offset of the memory map.
  extent_type offset() const noexcept { return _offset; }

  //! The size of the memory map.
  AFIO_MAKE_FREE_FUNCTION
  size_type length() const noexcept { return _length; }

  //! Ask the system to commit the system resources to make the memory represented by the buffer available with the given permissions. addr and length should be page aligned (see utils::page_sizes()), if not the returned buffer is the region actually committed.
  AFIO_HEADERS_ONLY_MEMFUNC_SPEC result<buffer_type> commit(buffer_type region, section_handle::flag _flag = section_handle::flag::readwrite) noexcept;

  //! Ask the system to make the memory represented by the buffer unavailable and to decommit the system resources representing them. addr and length should be page aligned (see utils::page_sizes()), if not the returned buffer is the region actually decommitted.
  AFIO_HEADERS_ONLY_MEMFUNC_SPEC result<buffer_type> decommit(buffer_type region) noexcept;

  /*! Zero the memory represented by the buffer. Differs from zero() because it acts on mapped memory, but may call zero() internally.

  On Linux, Windows and FreeBSD any full 4Kb pages will be deallocated from the
  system entirely, including the extents for them in any backing storage. On newer Linux kernels the kernel can additionally swap whole 4Kb pages for
  freshly zeroed ones making this a very efficient way of zeroing large ranges of memory.
  \errors Any of the errors returnable by madvise() or DiscardVirtualMemory or the zero() function.
  */
  AFIO_HEADERS_ONLY_MEMFUNC_SPEC result<void> zero_memory(buffer_type region) noexcept;

  /*! Ask the system to unset the dirty flag for the memory represented by the buffer. This will prevent any changes not yet sent to the backing storage from being sent in the future, also if the system kicks out this page and reloads it you may see some edition of the underlying storage instead of what was here. addr
  and length should be page aligned (see utils::page_sizes()), if not the returned buffer is the region actually undirtied.

  \warning This function destroys the contents of unwritten pages in the region in a totally unpredictable fashion. Only use it if you don't care how much of
  the region reaches physical storage or not. Note that the region is not necessarily zeroed, and may be randomly zeroed.

  \note Microsoft Windows does not support unsetting the dirty flag on file backed maps, so on Windows this call does nothing.
  */
  AFIO_HEADERS_ONLY_MEMFUNC_SPEC result<buffer_type> do_not_store(buffer_type region) noexcept;

  //! Ask the system to begin to asynchronously prefetch the span of memory regions given, returning the regions actually prefetched. Note that on Windows 7 or earlier the system call to implement this was not available, and so you will see an empty span returned.
  static AFIO_HEADERS_ONLY_MEMFUNC_SPEC result<span<buffer_type>> prefetch(span<buffer_type> regions) noexcept;
  //! \overload
  static result<buffer_type> prefetch(buffer_type region) noexcept
  {
    OUTCOME_TRY(ret, prefetch(span<buffer_type>(&region, 1)));
    return *ret.data();
  }

  /*! \brief Read data from the mapped view.

  \note Because this implementation never copies memory, you can pass in buffers with a null address.

  \return The buffers read, which will never be the buffers input because they will point into the mapped view.
  The size of each scatter-gather buffer is updated with the number of bytes of that buffer transferred.
  \param reqs A scatter-gather and offset request.
  \param d Ignored.
  \errors None, though the various signals and structured exception throws common to using memory maps may occur.
  \mallocs None.
  */
  AFIO_MAKE_FREE_FUNCTION
  AFIO_HEADERS_ONLY_VIRTUAL_SPEC io_result<buffers_type> read(io_request<buffers_type> reqs, deadline d = deadline()) noexcept override;
  using io_handle::read;

  /*! \brief Write data to the mapped view.

  \return The buffers written, which will never be the buffers input because they will point at where the data was copied into the mapped view.
  The size of each scatter-gather buffer is updated with the number of bytes of that buffer transferred.
  \param reqs A scatter-gather and offset request.
  \param d Ignored.
  \errors None, though the various signals and structured exception throws common to using memory maps may occur.
  \mallocs None.
  */
  AFIO_MAKE_FREE_FUNCTION
  AFIO_HEADERS_ONLY_VIRTUAL_SPEC io_result<const_buffers_type> write(io_request<const_buffers_type> reqs, deadline d = deadline()) noexcept override;
  using io_handle::write;
};

//! \brief Constructor for `map_handle`
template <> struct construct<map_handle>
{
  section_handle &section;
  map_handle::size_type bytes = 0;
  map_handle::extent_type offset = 0;
  section_handle::flag _flag = section_handle::flag::readwrite;
  result<map_handle> operator()() const noexcept { return map_handle::map(section, bytes, offset, _flag); }
};

// BEGIN make_free_functions.py
//! Swap with another instance
inline void swap(section_handle &self, section_handle &o) noexcept
{
  return self.swap(std::forward<decltype(o)>(o));
}

//! Return the current maximum permitted extent of the memory section.
inline result<section_handle::extent_type> length(const section_handle &self) noexcept
{
  return self.length();
}
/*! Resize the current maximum permitted extent of the memory section to the given extent.
\param self The object whose member function to call.
\param newsize The new size of the memory section. Specify zero to use `backing.length()`.
This cannot exceed the size of any backing file used.

\errors Any of the values NtExtendSection() can return. On POSIX this is a no op.
*/
inline result<section_handle::extent_type> truncate(section_handle &self, section_handle::extent_type newsize = 0) noexcept
{
  return self.truncate(std::forward<decltype(newsize)>(newsize));
}
//! Swap with another instance
inline void swap(map_handle &self, map_handle &o) noexcept
{
  return self.swap(std::forward<decltype(o)>(o));
}
//! Unmap the mapped view.
inline result<void> close(map_handle &self) noexcept
{
  return self.close();
}
inline map_handle::io_result<map_handle::const_buffers_type> barrier(map_handle &self, map_handle::io_request<map_handle::const_buffers_type> reqs = map_handle::io_request<map_handle::const_buffers_type>(), bool wait_for_device = false, bool and_metadata = false, deadline d = deadline()) noexcept
{
  return self.barrier(std::forward<decltype(reqs)>(reqs), std::forward<decltype(wait_for_device)>(wait_for_device), std::forward<decltype(and_metadata)>(and_metadata), std::forward<decltype(d)>(d));
}
/*! Create new memory and map it into view.
\param bytes How many bytes to create and map. Typically will be rounded to a multiple of the page size (see utils::page_sizes()).
\param _flag The permissions with which to map the view which are constrained by the permissions of the memory section. `flag::none` can be useful for reserving virtual address space without committing system resources, use commit() to later change availability of memory.

\note On Microsoft Windows this constructor uses the faster VirtualAlloc() which creates less versatile page backed memory. If you want anonymous memory
allocated from a paging file backed section instead, create a page file backed section and then a mapped view from that using
the other constructor. This makes available all those very useful VM tricks Windows can do with section mapped memory which
VirtualAlloc() memory cannot do.

\errors Any of the values POSIX mmap() or VirtualAlloc() can return.
*/
inline result<map_handle> map(map_handle::size_type bytes, section_handle::flag _flag = section_handle::flag::readwrite) noexcept
{
  return map_handle::map(std::forward<decltype(bytes)>(bytes), std::forward<decltype(_flag)>(_flag));
}
/*! Create a memory mapped view of a backing storage.
\param section A memory section handle specifying the backing storage to use.
\param bytes How many bytes to map (0 = the size of the memory section). Typically will be rounded to a multiple of the page size (see utils::page_sizes()).
\param offset The offset into the backing storage to map from. Typically needs to be at least a multiple of the page size (see utils::page_sizes()), on Windows it needs to be a multiple of the kernel memory allocation granularity (typically 64Kb).
\param _flag The permissions with which to map the view which are constrained by the permissions of the memory section. `flag::none` can be useful for reserving virtual address space without committing system resources, use commit() to later change availability of memory.

\errors Any of the values POSIX mmap() or NtMapViewOfSection() can return.
*/
inline result<map_handle> map(section_handle &section, map_handle::size_type bytes = 0, map_handle::extent_type offset = 0, section_handle::flag _flag = section_handle::flag::readwrite) noexcept
{
  return map_handle::map(std::forward<decltype(section)>(section), std::forward<decltype(bytes)>(bytes), std::forward<decltype(offset)>(offset), std::forward<decltype(_flag)>(_flag));
}
//! The size of the memory map.
inline map_handle::size_type length(const map_handle &self) noexcept
{
  return self.length();
}
/*! \brief Read data from the mapped view.

\note Because this implementation never copies memory, you can pass in buffers with a null address.

\return The buffers read, which will never be the buffers input because they will point into the mapped view.
The size of each scatter-gather buffer is updated with the number of bytes of that buffer transferred.
\param self The object whose member function to call.
\param reqs A scatter-gather and offset request.
\param d Ignored.
\errors None, though the various signals and structured exception throws common to using memory maps may occur.
\mallocs None.
*/
inline map_handle::io_result<map_handle::buffers_type> read(map_handle &self, map_handle::io_request<map_handle::buffers_type> reqs, deadline d = deadline()) noexcept
{
  return self.read(std::forward<decltype(reqs)>(reqs), std::forward<decltype(d)>(d));
}
/*! \brief Write data to the mapped view.

\return The buffers written, which will never be the buffers input because they will point at where the data was copied into the mapped view.
The size of each scatter-gather buffer is updated with the number of bytes of that buffer transferred.
\param self The object whose member function to call.
\param reqs A scatter-gather and offset request.
\param d Ignored.
\errors None, though the various signals and structured exception throws common to using memory maps may occur.
\mallocs None.
*/
inline map_handle::io_result<map_handle::const_buffers_type> write(map_handle &self, map_handle::io_request<map_handle::const_buffers_type> reqs, deadline d = deadline()) noexcept
{
  return self.write(std::forward<decltype(reqs)>(reqs), std::forward<decltype(d)>(d));
}
// END make_free_functions.py

AFIO_V2_NAMESPACE_END

#if AFIO_HEADERS_ONLY == 1 && !defined(DOXYGEN_SHOULD_SKIP_THIS)
#define AFIO_INCLUDED_BY_HEADER 1
#ifdef _WIN32
#include "detail/impl/windows/map_handle.ipp"
#else
#include "detail/impl/posix/map_handle.ipp"
#endif
#undef AFIO_INCLUDED_BY_HEADER
#endif

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#endif
