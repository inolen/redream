find_program(CLANG_FORMAT_EXECUTABLE NAMES clang-format clang-format-3.6)
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(cppcheck
  DEFAULT_MSG
  CLANG_FORMAT_EXECUTABLE)
