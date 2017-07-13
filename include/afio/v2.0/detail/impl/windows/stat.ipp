/* Information about a file
(C) 2015-2017 Niall Douglas <http://www.nedproductions.biz/> (4 commits)
File Created: Apr 2017


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

#include "../../../handle.hpp"
#include "../../../stat.hpp"
#include "import.hpp"

#include <winioctl.h>  // for DeviceIoControl codes

BOOST_AFIO_V2_NAMESPACE_BEGIN

BOOST_AFIO_HEADERS_ONLY_MEMFUNC_SPEC result<size_t> stat_t::fill(const handle &h, stat_t::want wanted) noexcept
{
  BOOST_AFIO_LOG_FUNCTION_CALL(h.native_handle().h);
  windows_nt_kernel::init();
  using namespace windows_nt_kernel;
  alignas(8) fixme_path::value_type buffer[32769];
  IO_STATUS_BLOCK isb = make_iostatus();
  NTSTATUS ntstat;
  size_t ret = 0;

  FILE_ALL_INFORMATION &fai=*(FILE_ALL_INFORMATION *)buffer;
  FILE_FS_SECTOR_SIZE_INFORMATION ffssi={0};
  bool needInternal=!!(wanted&want::ino);
  bool needBasic=(!!(wanted&want::type) || !!(wanted&want::atim) || !!(wanted&want::mtim) || !!(wanted&want::ctim) || !!(wanted&want::birthtim) || !!(wanted&want::sparse) || !!(wanted&want::compressed) || !!(wanted&want::reparse_point));
  bool needStandard=(!!(wanted&want::nlink) || !!(wanted&want::size) || !!(wanted&want::allocated) || !!(wanted&want::blocks));
  // It's not widely known that the NT kernel supplies a stat() equivalent i.e. get me everything in a single syscall
  // However fetching FileAlignmentInformation which comes with FILE_ALL_INFORMATION is slow as it touches the device driver,
  // so only use if we need more than one item
  if(((int) needInternal+(int) needBasic+(int) needStandard)>=2)
  {
    ntstat=NtQueryInformationFile(h.native_handle().h, &isb, &fai, sizeof(buffer), FileAllInformation);
    if(STATUS_PENDING == ntstat)
      ntstat = ntwait(h.native_handle().h, isb, deadline());
    if(ntstat)
      return make_errored_result_nt<size_t>(ntstat, last190(h.path().u8string()));
  }
  else
  {
    if(needInternal)
    {
      ntstat=NtQueryInformationFile(h.native_handle().h, &isb, &fai.InternalInformation, sizeof(fai.InternalInformation), FileInternalInformation);
      if(STATUS_PENDING == ntstat)
        ntstat = ntwait(h.native_handle().h, isb, deadline());
      if(ntstat)
        return make_errored_result_nt<size_t>(ntstat, last190(h.path().u8string()));
    }
    if(needBasic)
    {
      isb.Status = -1;
      ntstat=NtQueryInformationFile(h.native_handle().h, &isb, &fai.BasicInformation, sizeof(fai.BasicInformation), FileBasicInformation);
      if(STATUS_PENDING == ntstat)
        ntstat = ntwait(h.native_handle().h, isb, deadline());
      if(ntstat)
        return make_errored_result_nt<size_t>(ntstat, last190(h.path().u8string()));
    }
    if(needStandard)
    {
      isb.Status = -1;
      ntstat=NtQueryInformationFile(h.native_handle().h, &isb, &fai.StandardInformation, sizeof(fai.StandardInformation), FileStandardInformation);
      if(STATUS_PENDING == ntstat)
        ntstat = ntwait(h.native_handle().h, isb, deadline());
      if(ntstat)
        return make_errored_result_nt<size_t>(ntstat, last190(h.path().u8string()));
    }
  }
  if((wanted&want::blocks) || (wanted&want::blksize))
  {
    isb.Status = -1;
    ntstat=NtQueryVolumeInformationFile(h.native_handle().h, &isb, &ffssi, sizeof(ffssi), FileFsSectorSizeInformation);
    if(STATUS_PENDING == ntstat)
      ntstat = ntwait(h.native_handle().h, isb, deadline());
    if(ntstat)
      return make_errored_result_nt<size_t>(ntstat, last190(h.path().u8string()));
  }

  // FIXME: Implement st_dev for Windows somehow
  if(wanted&want::dev) { st_dev=0; }
  if(wanted&want::ino) { st_ino=fai.InternalInformation.IndexNumber.QuadPart; ++ret; }
  if(wanted&want::type)
  {
    ULONG ReparsePointTag=fai.EaInformation.ReparsePointTag;
    // We need to get its reparse tag to see if it's a symlink
    if(fai.BasicInformation.FileAttributes&FILE_ATTRIBUTE_REPARSE_POINT && !ReparsePointTag)
    {
      alignas(8) char buffer_[sizeof(REPARSE_DATA_BUFFER)+32769];
      DWORD written=0;
      REPARSE_DATA_BUFFER *rpd=(REPARSE_DATA_BUFFER *) buffer_;
      memset(rpd, 0, sizeof(*rpd));
      if(!DeviceIoControl(h.native_handle().h, FSCTL_GET_REPARSE_POINT, NULL, 0, rpd, sizeof(buffer_), &written, NULL))
        return make_errored_result<size_t>(GetLastError(), last190(h.path().u8string()));
      ReparsePointTag=rpd->ReparseTag;
    }
    st_type=windows_nt_kernel::to_st_type(fai.BasicInformation.FileAttributes, ReparsePointTag); ++ret;
  }
  if(wanted&want::nlink) { st_nlink=(int16_t) fai.StandardInformation.NumberOfLinks; ++ret; }
  if(wanted&want::atim) { st_atim=to_timepoint(fai.BasicInformation.LastAccessTime); ++ret; }
  if(wanted&want::mtim) { st_mtim=to_timepoint(fai.BasicInformation.LastWriteTime); ++ret; }
  if(wanted&want::ctim) { st_ctim=to_timepoint(fai.BasicInformation.ChangeTime); ++ret; }
  if(wanted&want::size) { st_size=fai.StandardInformation.EndOfFile.QuadPart; ++ret; }
  if(wanted&want::allocated) { st_allocated=fai.StandardInformation.AllocationSize.QuadPart; ++ret; }
  if(wanted&want::blocks) { st_blocks=fai.StandardInformation.AllocationSize.QuadPart/ffssi.PhysicalBytesPerSectorForPerformance; ++ret; }
  if(wanted&want::blksize) { st_blksize=(uint16_t) ffssi.PhysicalBytesPerSectorForPerformance; ++ret; }
  if(wanted&want::birthtim) { st_birthtim=to_timepoint(fai.BasicInformation.CreationTime); ++ret; }
  if(wanted&want::sparse) { st_sparse=!!(fai.BasicInformation.FileAttributes & FILE_ATTRIBUTE_SPARSE_FILE); ++ret; }
  if(wanted&want::compressed) { st_compressed=!!(fai.BasicInformation.FileAttributes & FILE_ATTRIBUTE_COMPRESSED); ++ret; }
  if(wanted&want::reparse_point) { st_reparse_point=!!(fai.BasicInformation.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT); ++ret; }
  return ret;
}

BOOST_AFIO_V2_NAMESPACE_END
