# CMake generated Testfile for 
# Source directory: /Users/amirmohammadtavakkoli/project/UNIX-Command-Line-Interface
# Build directory: /Users/amirmohammadtavakkoli/project/UNIX-Command-Line-Interface/build
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(TokenizerTests "/Users/amirmohammadtavakkoli/project/UNIX-Command-Line-Interface/build/test_tokenizer")
set_tests_properties(TokenizerTests PROPERTIES  _BACKTRACE_TRIPLES "/Users/amirmohammadtavakkoli/project/UNIX-Command-Line-Interface/CMakeLists.txt;58;add_test;/Users/amirmohammadtavakkoli/project/UNIX-Command-Line-Interface/CMakeLists.txt;0;")
add_test(IntegrationTests "/Users/amirmohammadtavakkoli/project/UNIX-Command-Line-Interface/build/test_integration")
set_tests_properties(IntegrationTests PROPERTIES  ENVIRONMENT "TASH_SHELL_BIN=/Users/amirmohammadtavakkoli/project/UNIX-Command-Line-Interface/build/shell.out" _BACKTRACE_TRIPLES "/Users/amirmohammadtavakkoli/project/UNIX-Command-Line-Interface/CMakeLists.txt;77;add_test;/Users/amirmohammadtavakkoli/project/UNIX-Command-Line-Interface/CMakeLists.txt;0;")
subdirs("_deps/googletest-build")
