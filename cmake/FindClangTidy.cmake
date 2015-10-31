find_program(CLANG_TIDY_EXECUTABLE NAMES clang-tidy clang-tidy-3.6 clang-tidy-3.7)
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(cppcheck
  DEFAULT_MSG
  CLANG_TIDY_EXECUTABLE)
