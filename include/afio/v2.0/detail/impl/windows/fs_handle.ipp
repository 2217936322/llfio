/* A filing system handle
(C) 2017 Niall Douglas <http://www.nedproductions.biz/> (20 commits)
File Created: Aug 2017


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

#include "../../../fs_handle.hpp"
#include "../../../stat.hpp"
#include "../../../utils.hpp"
#include "import.hpp"

AFIO_V2_NAMESPACE_BEGIN

result<void> fs_handle::_fetch_inode() noexcept
{
  stat_t s;
  OUTCOME_TRYV(s.fill(_get_handle(), stat_t::want::dev | stat_t::want::ino));
  _devid = s.st_dev;
  _inode = s.st_ino;
  return success();
}

result<path_handle> fs_handle::parent_path_handle(deadline d) const noexcept
{
  windows_nt_kernel::init();
  using namespace windows_nt_kernel;
  AFIO_LOG_FUNCTION_CALL(this);
  auto &h = _get_handle();
  AFIO_WIN_DEADLINE_TO_SLEEP_INIT(d);
  try
  {
    for(;;)
    {
      // Get current path for handle and open its containing dir
      auto currentpath_ = h.current_path();
      if(!currentpath_)
        continue;
      filesystem::path currentpath = std::move(currentpath_.value());
      // If current path is empty, it's been deleted
      if(currentpath.empty())
        return std::errc::no_such_file_or_directory;
      // Split the path into root and leafname
      filesystem::path filename = currentpath.filename();
      currentpath.remove_filename();
      /* We have to be super careful here because \Device\HarddiskVolume4 != \Device\HarddiskVolume4\!
      The former opens the device, the latter the root directory of the device.
      */
      const_cast<filesystem::path::string_type &>(currentpath.native()).push_back('\\');
      auto currentdirh_ = path_handle::path(currentpath);
      if(!currentdirh_)
        continue;
      path_handle currentdirh = std::move(currentdirh_.value());
      if(h.flags() & handle::flag::disable_safety_unlinks)
        return success(std::move(currentdirh));

      DWORD fileshare = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
      IO_STATUS_BLOCK isb = make_iostatus();
      path_view::c_str zpath(filename, true);
      UNICODE_STRING _path;
      _path.Buffer = const_cast<wchar_t *>(zpath.buffer);
      _path.MaximumLength = (_path.Length = (USHORT)(zpath.length * sizeof(wchar_t))) + sizeof(wchar_t);
      OBJECT_ATTRIBUTES oa;
      memset(&oa, 0, sizeof(oa));
      oa.Length = sizeof(OBJECT_ATTRIBUTES);
      oa.ObjectName = &_path;
      oa.RootDirectory = currentdirh.native_handle().h;
      LARGE_INTEGER AllocationSize;
      memset(&AllocationSize, 0, sizeof(AllocationSize));
      HANDLE nh = nullptr;
      NTSTATUS ntstat = NtCreateFile(&nh, SYNCHRONIZE, &oa, &isb, &AllocationSize, 0, fileshare, 0x00000001 /*FILE_OPEN*/, 0x20 /*FILE_SYNCHRONOUS_IO_NONALERT*/, NULL, 0);
      if(STATUS_SUCCESS != ntstat)
        continue;
      auto unnh = undoer([nh] { CloseHandle(nh); });
      (void) unnh;
      isb.Status = -1;
      FILE_INTERNAL_INFORMATION fii;
      ntstat = NtQueryInformationFile(nh, &isb, &fii, sizeof(fii), FileInternalInformation);
      if(STATUS_SUCCESS != ntstat)
        continue;
      // If the same, we know for a fact that this is the correct containing dir for now at least
      // FIXME: We are not comparing device number, that's faked as the volume number in stat_t
      if((ino_t) fii.IndexNumber.QuadPart == _inode)
        return success(std::move(currentdirh));
      AFIO_WIN_DEADLINE_TO_TIMEOUT(d);
    }
  }
  catch(...)
  {
    return error_from_exception();
  }
}

result<void> fs_handle::relink(const path_handle &base, path_view_type path, bool atomic_replace, deadline d) noexcept
{
  windows_nt_kernel::init();
  using namespace windows_nt_kernel;
  AFIO_LOG_FUNCTION_CALL(this);
  auto &h = _get_handle();

  // If the target is a win32 path, we need to convert to NT path and call ourselves
  if(!base.is_valid() && !path.is_ntpath())
  {
    path_view::c_str zpath(path, false);
    UNICODE_STRING NtPath;
    if(!RtlDosPathNameToNtPathName_U(zpath.buffer, &NtPath, NULL, NULL))
    {
      return {ERROR_FILE_NOT_FOUND, std::system_category()};
    }
    auto unntpath = undoer([&NtPath] {
      if(!HeapFree(GetProcessHeap(), 0, NtPath.Buffer))
      {
        abort();
      }
    });
    // RtlDosPathNameToNtPathName_U outputs \??\path, so path.is_ntpath() will be false.
    return relink(base, wstring_view(NtPath.Buffer, NtPath.Length / sizeof(wchar_t)));
  }

  path_view::c_str zpath(path, true);
  UNICODE_STRING _path;
  _path.Buffer = const_cast<wchar_t *>(zpath.buffer);
  _path.MaximumLength = (_path.Length = (USHORT)(zpath.length * sizeof(wchar_t))) + sizeof(wchar_t);
  if(zpath.length >= 4 && _path.Buffer[0] == '\\' && _path.Buffer[1] == '!' && _path.Buffer[2] == '!' && _path.Buffer[3] == '\\')
  {
    _path.Buffer += 3;
    _path.Length -= 3 * sizeof(wchar_t);
    _path.MaximumLength -= 3 * sizeof(wchar_t);
  }
  IO_STATUS_BLOCK isb = make_iostatus();
  alignas(8) char buffer[sizeof(FILE_RENAME_INFORMATION) + 65536];
  FILE_RENAME_INFORMATION *fni = (FILE_RENAME_INFORMATION *) buffer;
  fni->ReplaceIfExists = atomic_replace;
  fni->RootDirectory = base.is_valid() ? base.native_handle().h : nullptr;
  fni->FileNameLength = _path.Length;
  memcpy(fni->FileName, _path.Buffer, fni->FileNameLength);
  NTSTATUS ntstat = NtSetInformationFile(h.native_handle().h, &isb, fni, sizeof(FILE_RENAME_INFORMATION) + fni->FileNameLength, FileRenameInformation);
  if(STATUS_PENDING == ntstat)
    ntstat = ntwait(h.native_handle().h, isb, d);
  if(ntstat < 0)
  {
    return {(int) ntstat, ntkernel_category()};
  }
  return success();
}

result<void> fs_handle::unlink(deadline d) noexcept
{
  using flag = handle::flag;
  windows_nt_kernel::init();
  using namespace windows_nt_kernel;
  AFIO_LOG_FUNCTION_CALL(this);
  auto &h = _get_handle();
  if((h.is_regular() || h.is_symlink()) && !(h.flags() & flag::win_disable_unlink_emulation))
  {
    // Rename it to something random to emulate immediate unlinking
    auto randomname = utils::random_string(32);
    randomname.append(".deleted");
    OUTCOME_TRY(dirh, parent_path_handle(d));
    result<void> out = relink(dirh, randomname);
    if(!out)
    {
      // If something else is using it, we may not be able to rename
      if(out.error().value() != (int) 0xC0000043 /*STATUS_SHARING_VIOLATION*/)
      {
        return out.error();
      }
    }
  }
  // No point marking it for deletion if it's already been so
  if(!(h.flags() & flag::unlink_on_close))
  {
    // Hide the item in Explorer and the command line
    {
      IO_STATUS_BLOCK isb = make_iostatus();
      FILE_BASIC_INFORMATION fbi;
      memset(&fbi, 0, sizeof(fbi));
      fbi.FileAttributes = FILE_ATTRIBUTE_HIDDEN;
      NTSTATUS ntstat = NtSetInformationFile(h.native_handle().h, &isb, &fbi, sizeof(fbi), FileBasicInformation);
      if(STATUS_PENDING == ntstat)
        ntstat = ntwait(h.native_handle().h, isb, d);
      (void) ntstat;
    }
    // Mark the item as delete on close
    IO_STATUS_BLOCK isb = make_iostatus();
    FILE_DISPOSITION_INFORMATION fdi;
    memset(&fdi, 0, sizeof(fdi));
    fdi._DeleteFile = true;
    NTSTATUS ntstat = NtSetInformationFile(h.native_handle().h, &isb, &fdi, sizeof(fdi), FileDispositionInformation);
    if(STATUS_PENDING == ntstat)
      ntstat = ntwait(h.native_handle().h, isb, d);
    if(ntstat < 0)
    {
      return {(int) ntstat, ntkernel_category()};
    }
  }
  return success();
}

AFIO_V2_NAMESPACE_END
