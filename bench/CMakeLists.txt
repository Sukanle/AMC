# Google Benchmark baselines. Targets are only added when AMC_BUILD_BENCHMARKS
# is enabled and the Google Benchmark package was found by Dependencies.cmake.

add_executable(amc-pass-perf-bench pass_perf_bench.cpp)
target_include_directories(amc-pass-perf-bench PRIVATE ${AMC_INCLUDE_DIR})
target_link_libraries(amc-pass-perf-bench PRIVATE amc-core amc-pass benchmark::benchmark_main)
set_target_properties(amc-pass-perf-bench PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
add_test(NAME amc-pass-perf-bench COMMAND amc-pass-perf-bench --benchmark_min_time=0.05s)
set_tests_properties(amc-pass-perf-bench PROPERTIES LABELS perf)

add_executable(amc-artifact-perf-bench artifact_perf_bench.cpp)
target_include_directories(amc-artifact-perf-bench PRIVATE ${AMC_INCLUDE_DIR})
target_link_libraries(amc-artifact-perf-bench PRIVATE amc-core benchmark::benchmark_main)
set_target_properties(amc-artifact-perf-bench PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
add_test(NAME amc-artifact-perf-bench COMMAND amc-artifact-perf-bench --benchmark_min_time=0.05s)
set_tests_properties(amc-artifact-perf-bench PROPERTIES LABELS perf)

add_executable(amc-cache-perf-bench cache_perf_bench.cpp)
target_include_directories(amc-cache-perf-bench PRIVATE ${AMC_INCLUDE_DIR})
target_link_libraries(amc-cache-perf-bench PRIVATE amc-core amc-compilation benchmark::benchmark_main)
set_target_properties(amc-cache-perf-bench PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
add_test(NAME amc-cache-perf-bench COMMAND amc-cache-perf-bench --benchmark_min_time=0.05s)
set_tests_properties(amc-cache-perf-bench PROPERTIES LABELS perf)

add_executable(amc-query-perf-bench query_perf_bench.cpp)
target_include_directories(amc-query-perf-bench PRIVATE ${AMC_INCLUDE_DIR})
target_link_libraries(amc-query-perf-bench PRIVATE amc-core benchmark::benchmark_main)
set_target_properties(amc-query-perf-bench PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
add_test(NAME amc-query-perf-bench COMMAND amc-query-perf-bench --benchmark_min_time=0.05s)
set_tests_properties(amc-query-perf-bench PROPERTIES LABELS perf)

add_executable(amc-multifile-perf-bench multifile_perf_bench.cpp)
target_include_directories(amc-multifile-perf-bench PRIVATE ${AMC_INCLUDE_DIR})
target_link_libraries(amc-multifile-perf-bench PRIVATE amc-core benchmark::benchmark_main)
set_target_properties(amc-multifile-perf-bench PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
add_test(NAME amc-multifile-perf-bench COMMAND amc-multifile-perf-bench --benchmark_min_time=0.05s)
set_tests_properties(amc-multifile-perf-bench PROPERTIES LABELS perf)
