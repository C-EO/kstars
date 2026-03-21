# Tests/shared — Header-only shared test infrastructure
#
# This directory provides reusable test utilities for all KStars test
# subdirectories.  It is header-only: no compilation step is needed.
# CMakeLists.txt files in other test directories only need to add:
#
#   include_directories(${kstars_SOURCE_DIR}/Tests/shared)
#
# The top-level Tests/CMakeLists.txt already does this globally, so normally
# nothing needs to be done in individual test CMakeLists.txt files.
#
# Headers provided:
#   kstars_test_macros.h  — Generic Qt Test macros (KVERIFY_SUB, KTRY_CLICK, …)
#
# See README.md for full documentation.
