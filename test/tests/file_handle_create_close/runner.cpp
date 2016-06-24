/* Integration test kernel for file_handle create and close
(C) 2016 Niall Douglas http://www.nedprod.com/
File Created: Apr 2016
*/

#include "../../kerneltest/include/boost/kerneltest/v1/hooks/filesystem_workspace.hpp"
#include "../../kerneltest/include/boost/kerneltest/v1/permute_parameters.hpp"
#include "../../kerneltest/include/boost/kerneltest/v1/test_kernel.hpp"
#include "kernel_async_file_handle.cpp.hpp"
#include "kernel_file_handle.cpp.hpp"

template <class U> inline void file_handle_create_close_creation(U &&f)
{
  using namespace BOOST_KERNELTEST_V1_NAMESPACE;
  using file_handle = BOOST_AFIO_V2_NAMESPACE::file_handle;

  /* Set up a permuter which for every one of these parameter values listed,
  tests with the value using the input workspace which should produce outcome
  workspace. Workspaces are:
    * non-existing: no files
    *    existing0: single zero length file
    *    existing1: single one byte length file
  */
  // clang-format off
  static const auto permuter(mt_permute_parameters<  // This is a multithreaded parameter permutation test
    result<void>,                                    // The output outcome/result/option type. Type void means we don't care about the return type.
    parameters<                                      // The types of one or more input parameters to permute/fuzz the kernel with.
      typename file_handle::creation
    >,
    // Any additional per-permute parameters not used to invoke the kernel
    hooks::filesystem_setup_parameters,
    hooks::filesystem_comparison_structure_parameters
  >(
    { // Initialiser list of output value expected for the input parameters, plus any hook parameters
      { make_errored_result<void>(ENOENT), { file_handle::creation::open_existing     }, { "non-existing" }, { "non-existing" }},
      {   make_ready_result<void>(),       { file_handle::creation::open_existing     }, { "existing0"    }, { "existing0"    }},
      {   make_ready_result<void>(),       { file_handle::creation::open_existing     }, { "existing1"    }, { "existing1"    }},
      {   make_ready_result<void>(),       { file_handle::creation::only_if_not_exist }, { "non-existing" }, { "existing0"    }},
      { make_errored_result<void>(EEXIST), { file_handle::creation::only_if_not_exist }, { "existing0"    }, { "existing0"    }},
      {   make_ready_result<void>(),       { file_handle::creation::if_needed         }, { "non-existing" }, { "existing0"    }},
      {   make_ready_result<void>(),       { file_handle::creation::if_needed         }, { "existing1"    }, { "existing1"    }},
      { make_errored_result<void>(ENOENT), { file_handle::creation::truncate          }, { "non-existing" }, { "non-existing" }},
      {   make_ready_result<void>(),       { file_handle::creation::truncate          }, { "existing0"    }, { "existing0"    }},
      {   make_ready_result<void>(),       { file_handle::creation::truncate          }, { "existing1"    }, { "existing0"    }}
    },
    // Any parameters from now on are called before each permutation and the object returned is
    // destroyed after each permutation. The callspec is (parameter_permuter<...> *parent, outcome<T> &testret, size_t, pars)
    hooks::filesystem_setup("file_handle_create_close"),               // Configure this filesystem workspace before the test
    hooks::filesystem_comparison_structure("file_handle_create_close") // Do a structural comparison of the filesystem workspace after the test
  ));
  // clang-format on

  // Have the permuter permute callable f with all the permutations, returning outcomes
  auto results = permuter(std::forward<U>(f));
  std::vector<std::function<void()>> checks;

  // Check each of the results. We don't do this inside the kernel as it messes with fuzzing/sanitising.
  // We don't do this inside the parameter permuter either in case the fuzzer/sanitiser uses that.
  permuter.check(results, pretty_print_failure(permuter, [&](const auto &result, const auto &shouldbe) { checks.push_back([&] { BOOST_CHECK(result == shouldbe); }); }));

  for(auto &i : checks)
    i();
}

BOOST_KERNELTEST_TEST_KERNEL(integration, afio, file_handle_create_close, file_handle, "Tests that afio::file_handle's creation parameter works as expected", file_handle_create_close_creation(file_handle_create_close::test_kernel_file_handle))
BOOST_KERNELTEST_TEST_KERNEL(integration, afio, file_handle_create_close, async_file_handle, "Tests that afio::async_file_handle's creation parameter works as expected", file_handle_create_close_creation(file_handle_create_close::test_kernel_async_file_handle))
