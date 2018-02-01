# CTest script for a CI to submit to CDash a run of configuration,
# building and testing
cmake_minimum_required(VERSION 3.1 FATAL_ERROR)
include(cmake/QuickCppLibBootstrap.cmake)
include(QuickCppLibUtils)


CONFIGURE_CTEST_SCRIPT_FOR_CDASH("afio" "prebuilt")
ctest_empty_binary_directory(${CTEST_BINARY_DIRECTORY})
include(FindGit)
set(CTEST_GIT_COMMAND "${GIT_EXECUTABLE}")

ctest_start("Experimental")
ctest_update()
ctest_configure(OPTIONS ${CTEST_CONFIGURE_OPTIONS})
ctest_build(TARGET _dl)
ctest_build(TARGET _sl)
ctest_test(RETURN_VALUE retval EXCLUDE "afio_hl|shared_fs_mutex")
if(WIN32)
  if(EXISTS "prebuilt/bin/Release/afio_dl-2.0-Windows-x64-Release.dll")
    checked_execute_process("Tarring up binaries 1"
      COMMAND "${CMAKE_COMMAND}" -E make_directory afio/prebuilt/bin/Release
      COMMAND "${CMAKE_COMMAND}" -E make_directory afio/prebuilt/lib/Release
      COMMAND xcopy doc afio\\doc\\ /s /q
      COMMAND xcopy include afio\\include\\ /s /q
    )
    checked_execute_process("Tarring up binaries 2"
      COMMAND "${CMAKE_COMMAND}" -E copy Readme.md afio/
      COMMAND "${CMAKE_COMMAND}" -E copy release_notes.md afio/
    )
    checked_execute_process("Tarring up binaries 3"
      COMMAND "${CMAKE_COMMAND}" -E copy prebuilt/lib/Release/afio_sl-2.0-Windows-x64-Release.lib afio/prebuilt/lib/Release/
      COMMAND "${CMAKE_COMMAND}" -E copy prebuilt/lib/Release/ntkernel-error-category_sl.lib afio/prebuilt/lib/Release/
    )
    checked_execute_process("Tarring up binaries 4"
      COMMAND "${CMAKE_COMMAND}" -E copy prebuilt/lib/Release/afio_dl-2.0-Windows-x64-Release.lib afio/prebuilt/lib/Release/
      COMMAND "${CMAKE_COMMAND}" -E copy prebuilt/lib/Release/ntkernel-error-category_dl.lib afio/prebuilt/lib/Release/
    )
    checked_execute_process("Tarring up binaries 5"
      COMMAND "${CMAKE_COMMAND}" -E copy prebuilt/bin/Release/afio_dl-2.0-Windows-x64-Release.dll afio/prebuilt/bin/Release/
      COMMAND "${CMAKE_COMMAND}" -E copy prebuilt/bin/Release/ntkernel-error-category_dl.dll afio/prebuilt/bin/Release/
    )
    checked_execute_process("Tarring up binaries final"
      COMMAND 7z a -tzip afio-v2.0-binaries-win64.zip afio\\
    )
    get_filename_component(toupload afio-v2.0-binaries-win64.zip ABSOLUTE)
  endif()
else()
  if(EXISTS "prebuilt/lib/libafio_dl-2.0-Linux-x86_64-Release.so")
    checked_execute_process("Tarring up binaries"
      COMMAND mkdir afio
      COMMAND cp -a doc afio/
      COMMAND cp -a include afio/
      COMMAND cp -a Readme.md afio/
      COMMAND cp -a release_notes.md afio/
      COMMAND cp -a --parents prebuilt/lib/libafio_sl-2.0-Linux-x86_64-Release.a afio/
      COMMAND cp -a --parents prebuilt/lib/libafio_dl-2.0-Linux-x86_64-Release.so afio/
      COMMAND "${CMAKE_COMMAND}" -E tar cfz afio-v2.0-binaries-linux64.tgz afio
    )
    get_filename_component(toupload afio-v2.0-binaries-linux64.tgz ABSOLUTE)
  endif()
  if(EXISTS "prebuilt/lib/libafio_dl-2.0-Darwin-x86_64-Release.so")
    checked_execute_process("Tarring up binaries"
      COMMAND mkdir afio
      COMMAND cp -a doc afio/
      COMMAND cp -a include afio/
      COMMAND cp -a Readme.md afio/
      COMMAND cp -a release_notes.md afio/
      COMMAND cp -a --parents prebuilt/lib/libafio_sl-2.0-Darwin-x86_64-Release.a afio/
      COMMAND cp -a --parents prebuilt/lib/libafio_dl-2.0-Darwin-x86_64-Release.so afio/
      COMMAND "${CMAKE_COMMAND}" -E tar cfz afio-v2.0-binaries-darwin64.tgz afio
    )
    get_filename_component(toupload afio-v2.0-binaries-darwin64.tgz ABSOLUTE)
  endif()
endif()
set(retval2 0)
set(retval3 0)
if(("$ENV{CXX}" MATCHES "clang") OR ("$ENV{CXX}" MATCHES "g\\+\\+"))
  if("$ENV{CXX}" MATCHES "clang")
    ctest_build(TARGET _sl-asan)
    set(CTEST_CONFIGURATION_TYPE "asan")
    ctest_test(RETURN_VALUE retval2 INCLUDE "afio_sl" EXCLUDE "shared_fs_mutex")
  else()
    set(retval2 0)
  endif()
  ctest_build(TARGET _sl-ubsan)
  set(CTEST_CONFIGURATION_TYPE "ubsan")
  ctest_test(RETURN_VALUE retval3 INCLUDE "afio_sl" EXCLUDE "shared_fs_mutex")
endif()
merge_junit_results_into_ctest_xml()
if(EXISTS "${toupload}")
  ctest_upload(FILES "${toupload}")
endif()
ctest_submit()
if(NOT retval EQUAL 0 OR NOT retval2 EQUAL 0 OR NOT retval3 EQUAL 0)
  message(FATAL_ERROR "FATAL: Running tests exited with ${retval} ${retval2} ${retval3}")
endif()
