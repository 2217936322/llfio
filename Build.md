# Build instructions

These compilers and OSs are regularly tested:

- [GCC](https://gcc.gnu.org/) 7.4 (Linux 4.x x64)
- [Clang](https://clang.llvm.org/) 4.0 (Linux 4.x x64)
- Clang 5.0 (macOS 10.12 x64)
- Visual Studio 2017 (Windows 10 x64). Note that LLFIO does not currently
compile with `/permissive-` due a bug in MSVC.

Other compilers, architectures and OSs may work, but are not tested regularly.
You will need a working [Filesystem TS](https://en.cppreference.com/w/cpp/experimental/fs)
implementation in your STL, and at least C++ 14. 

## Get a copy of the source

Download [this archive](https://dedi5.nedprod.com/static/files/llfio-v2.0-source-latest.tar.xz)
or clone from the GitHub repository:

~~~
git config --system core.longpaths true
git clone --recursive https://github.com/ned14/llfio.git
cd llfio
~~~

The first command is relevant so deeply nested paths on Windows will work when
cloning the repository and submodules. It may require elevated privileges, but
you can also use `git config --global core.longpaths true` instead.

### If you already cloned before reading this

If you had already cloned _this_ repository, but didn't use the `--recursive`
switch, you can simply run the following command from inside the work tree:

~~~
git submodule update --init --recursive
~~~

## Header only usage

LLFIO defaults to header only library configuration, so you don't actually need
any of the prebuilt binaries below, or to build anything. Simply:

~~~cpp
#include "llfio/include/llfio.hpp"
~~~

Note that on Microsoft Windows, the default header only configuration is unsafe
to use outside of toy projects. You will get warnings of the form:

~~~
warning : LLFIO_HEADERS_ONLY=1, LLFIO_EXPERIMENTAL_STATUS_CODE=0 and NTKERNEL_ERROR_CATEGORY_INLINE=1 on Windows, this can produce unreliable binaries where semantic comparisons of error codes randomly fail!
~~~

... and ...

~~~
Defining custom error code category ntkernel_category() via header only form is unreliable! Semantic comparisons will break! Define NTKERNEL_ERROR_CATEGORY_INLINE to 0 and only ever link in ntkernel_category() from a prebuilt shared library to avoid this problem.
~~~

The cause is that `<system_error>` has a design flaw not rectified until
([probably](https://wg21.link/P1196)) C++ 20 where custom error code categories
are unsafe when shared libraries are in use.

You have one of three choices here: (i) Use [experimental SG14 `status_code`](https://wg21.link/P1028)
which doesn't have this problem (define `LLFIO_EXPERIMENTAL_STATUS_CODE=1`)
(ii) Use the NT kernel error category as a shared library ([see its
documentation](https://github.com/ned14/ntkernel-error-category)) (iii)
Don't use header only LLFIO on Windows (see below).

## Prebuilt binaries

It is faster to build programs using LLFIO if you don't use a header only build.
In this situation, define `LLFIO_HEADERS_ONLY=0`, and choose one of `LLFIO_DYN_LINK` or `LLFIO_STATIC_LINK` depending on whether you are using the prebuilt shared or static libraries respectively.

- https://dedi5.nedprod.com/static/files/llfio-v2.0-binaries-linux64-latest.tgz
- https://dedi5.nedprod.com/static/files/llfio-v2.0-binaries-win64-latest.zip

## Build static libraries from source

You will need [CMake](https://cmake.org/) installed, v3.5 or better. It is important to do an out-of-tree build, because the build will otherwise fail.

If you want C++ Coroutines support, you will need a C++ Coroutines supporting compiler. It should get automatically discovered and detected, however note that on Linux you currently need a very recent clang combined with a very recent libc++ as no recent GCC implements C++ Coroutines yet. For Debian/Ubuntu, `apt install libc++-dev-9 libc++abi-dev-9` might do it.

To build and test on POSIX (`make`, `ninja` etc):

~~~
mkdir build
cd build
cmake ..
cmake --build .
ctest -R llfio_sl
~~~

To build and test on Windows or Mac OS (Visual Studio, XCode etc):

~~~
mkdir build
cd build
cmake .. -G<your generator here>
cmake --build . --config Release
ctest -C Release -R llfio_sl
~~~

## Build shared libraries from source

You will need [CMake](https://cmake.org/) installed, v3.5 or better. It is important to do an out-of-tree build, because the build will otherwise fail.

To build and test on POSIX (`make`, `ninja` etc):

~~~
mkdir build
cd build
cmake ..
cmake --build . -- _dl
ctest -R llfio_dl
~~~

To build and test on Windows or Mac OS (Visual Studio, XCode etc):

~~~
mkdir build
cd build
cmake .. -G<your generator here>
cmake --build . --config Release --target _dl
ctest -C Release -R llfio_dl
~~~

## Installing libraries from source

~~~
mkdir build
cd build
cmake ..
cmake --build . -- _dl _sl _hl
cmake --build . --target install
~~~
