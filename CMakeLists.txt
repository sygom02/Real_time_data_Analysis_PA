cmake_minimum_required(VERSION 3.8)
project(cartographer_parallel)

option(BUILD_CUDA_TASK "Build CUDA score_all experiment modules" OFF)

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 14)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(catkin REQUIRED COMPONENTS
  roscpp
  geometry_msgs
  nav_msgs
  sensor_msgs
  std_msgs
  visualization_msgs
)
find_package(OpenMP)

catkin_package(
  INCLUDE_DIRS include
  LIBRARIES assignment_cpu_lib fast_matcher_lib
  CATKIN_DEPENDS roscpp geometry_msgs nav_msgs sensor_msgs std_msgs visualization_msgs
)

include_directories(
  include
  ${catkin_INCLUDE_DIRS}
)

set(ASSIGNMENT_SCORE_ALL_SOURCES
  src/score_all_dispatch.cpp
  src/score_all_cpu_baseline.cpp
  src/score_all_cpu_parallel.cpp
  src/score_all_cpu_opt_offsets.cpp
  src/score_all_cpu_opt_bounds.cpp
  src/score_all_cpu_opt.cpp
  src/score_all_cpu_omp.cpp
  src/score_all_cpu_opt_omp.cpp
)

if(BUILD_CUDA_TASK)
  enable_language(CUDA)
  set(CMAKE_CUDA_ARCHITECTURES 53)
  find_package(CUDAToolkit REQUIRED)
  list(APPEND ASSIGNMENT_SCORE_ALL_SOURCES
    src/score_all_gpu_thread_per_candidate.cu
    src/score_all_gpu_thread_cached_map.cu
    src/score_all_gpu_thread_pruned.cu
    src/score_all_gpu_block_per_candidate.cu
    src/score_all_gpu_block_cached_map_shared.cu
    src/score_all_gpu_block_cached_map_shared_pruned.cu
    src/score_all_gpu_block_cached_map_shared_pinned.cu
    src/score_all_gpu_block_cached_map_shared_reuse.cu
  )
else()
  list(APPEND ASSIGNMENT_SCORE_ALL_SOURCES
    src/score_all_cuda_stubs.cpp
  )
endif()

add_library(assignment_cpu_lib
  ${ASSIGNMENT_SCORE_ALL_SOURCES}
)
target_include_directories(assignment_cpu_lib PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

if(OpenMP_CXX_FOUND)
  target_link_libraries(assignment_cpu_lib OpenMP::OpenMP_CXX)
endif()

if(BUILD_CUDA_TASK)
  target_compile_definitions(assignment_cpu_lib PRIVATE
    CARTOGRAPHER_PARALLEL_CUDA_BUILD=1
  )
  target_link_libraries(assignment_cpu_lib CUDA::cudart)
  set_target_properties(assignment_cpu_lib PROPERTIES
    CUDA_STANDARD 14
    CUDA_STANDARD_REQUIRED ON
    CUDA_ARCHITECTURES 53
  )
endif()

add_library(fast_matcher_lib
  src/fast_matcher.cpp
)
target_include_directories(fast_matcher_lib PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_link_libraries(fast_matcher_lib assignment_cpu_lib)

add_executable(fast_correlative_node src/fast_correlative_node.cpp)
target_link_libraries(fast_correlative_node
  fast_matcher_lib
  ${catkin_LIBRARIES}
)

install(TARGETS
  assignment_cpu_lib
  fast_matcher_lib
  fast_correlative_node
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION lib/${PROJECT_NAME}
)

install(DIRECTORY include/${PROJECT_NAME}/
  DESTINATION include/${PROJECT_NAME}
)

install(DIRECTORY launch maps bags
  DESTINATION share/${PROJECT_NAME}
)
