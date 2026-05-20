#ifndef CARTOGRAPHER_PARALLEL_SCORE_ALL_BACKENDS_H_
#define CARTOGRAPHER_PARALLEL_SCORE_ALL_BACKENDS_H_

#include "cartographer_parallel/assignment.h"

#include <vector>

namespace cartographer_parallel {

// CPU baseline module: original nested-loop score_all implementation.
void score_all_cpu_baseline(const std::vector<unsigned char>& grid, int w,
                            int h, const std::vector<int>& px,
                            const std::vector<int>& py,
                            const std::vector<int>& cx,
                            const std::vector<int>& cy,
                            std::vector<float>* score,
                            ScoreAllRunStats* stats);

// CPU parallel-only module: original nested-loop scoring split by candidate.
void score_all_cpu_parallel(const std::vector<unsigned char>& grid, int w,
                            int h, const std::vector<int>& px,
                            const std::vector<int>& py,
                            const std::vector<int>& cx,
                            const std::vector<int>& cy,
                            std::vector<float>* score,
                            ScoreAllRunStats* stats);

// CPU offset-only module: precomputed y*w+x scan offsets with normal bounds.
void score_all_cpu_opt_offsets(const std::vector<unsigned char>& grid, int w,
                               int h, const std::vector<int>& px,
                               const std::vector<int>& py,
                               const std::vector<int>& cx,
                               const std::vector<int>& cy,
                               std::vector<float>* score,
                               ScoreAllRunStats* stats);

// CPU bounds-only module: scan bounding box removes repeated bounds checks.
void score_all_cpu_opt_bounds(const std::vector<unsigned char>& grid, int w,
                              int h, const std::vector<int>& px,
                              const std::vector<int>& py,
                              const std::vector<int>& cx,
                              const std::vector<int>& cy,
                              std::vector<float>* score,
                              ScoreAllRunStats* stats);

// CPU optimized module: single-thread scoring with fewer repeated operations.
void score_all_cpu_opt(const std::vector<unsigned char>& grid, int w, int h,
                       const std::vector<int>& px,
                       const std::vector<int>& py,
                       const std::vector<int>& cx,
                       const std::vector<int>& cy,
                       std::vector<float>* score,
                       ScoreAllRunStats* stats);

// CPU OpenMP module: score_all_cpu_opt() style loop parallelized by candidate.
void score_all_cpu_omp(const std::vector<unsigned char>& grid, int w, int h,
                       const std::vector<int>& px,
                       const std::vector<int>& py,
                       const std::vector<int>& cx,
                       const std::vector<int>& cy,
                       std::vector<float>* score,
                       ScoreAllRunStats* stats);

// Explicit opt+OpenMP module name for combined CPU optimization experiments.
void score_all_cpu_opt_omp(const std::vector<unsigned char>& grid, int w,
                           int h, const std::vector<int>& px,
                           const std::vector<int>& py,
                           const std::vector<int>& cx,
                           const std::vector<int>& cy,
                           std::vector<float>* score,
                           ScoreAllRunStats* stats);

// TEST2 module: one CUDA thread computes one candidate score.
bool score_all_gpu_thread_per_candidate(const std::vector<unsigned char>& grid,
                                        int w, int h,
                                        const std::vector<int>& px,
                                        const std::vector<int>& py,
                                        const std::vector<int>& cx,
                                        const std::vector<int>& cy,
                                        std::vector<float>* score,
                                        ScoreAllRunStats* stats);

// Isolated TEST2+TEST4 module: thread-per-candidate plus cached grid only.
bool score_all_gpu_thread_cached_map(const std::vector<unsigned char>& grid,
                                     int w, int h,
                                     const std::vector<int>& px,
                                     const std::vector<int>& py,
                                     const std::vector<int>& cx,
                                     const std::vector<int>& cy,
                                     std::vector<float>* score,
                                     ScoreAllRunStats* stats);

// Isolated TEST2+TEST5 module: thread-per-candidate plus CPU-side pruning only.
bool score_all_gpu_thread_pruned(const std::vector<unsigned char>& grid, int w,
                                 int h, const std::vector<int>& px,
                                 const std::vector<int>& py,
                                 const std::vector<int>& cx,
                                 const std::vector<int>& cy,
                                 std::vector<float>* score,
                                 ScoreAllRunStats* stats);

// TEST3 module: one CUDA block computes one candidate score.
bool score_all_gpu_block_per_candidate(const std::vector<unsigned char>& grid,
                                       int w, int h,
                                       const std::vector<int>& px,
                                       const std::vector<int>& py,
                                       const std::vector<int>& cx,
                                       const std::vector<int>& cy,
                                       std::vector<float>* score,
                                       ScoreAllRunStats* stats);

// TEST3+TEST4 module: block-per-candidate plus cached grid and shared reduction.
bool score_all_gpu_block_cached_map_shared(
    const std::vector<unsigned char>& grid, int w, int h,
    const std::vector<int>& px, const std::vector<int>& py,
    const std::vector<int>& cx, const std::vector<int>& cy,
    std::vector<float>* score, ScoreAllRunStats* stats);

// TEST3+TEST4+TEST5 module: cached/shared GPU scoring plus CPU-side pruning.
bool score_all_gpu_block_cached_map_shared_pruned(
    const std::vector<unsigned char>& grid, int w, int h,
    const std::vector<int>& px, const std::vector<int>& py,
    const std::vector<int>& cx, const std::vector<int>& cy,
    std::vector<float>* score, ScoreAllRunStats* stats);

// TEST6 module: TEST4 plus reusable pinned host transfer buffers.
bool score_all_gpu_block_cached_map_shared_pinned(
    const std::vector<unsigned char>& grid, int w, int h,
    const std::vector<int>& px, const std::vector<int>& py,
    const std::vector<int>& cx, const std::vector<int>& cy,
    std::vector<float>* score, ScoreAllRunStats* stats);

// TEST7 module: TEST4 plus reusable GPU device buffers.
bool score_all_gpu_block_cached_map_shared_reuse_buffers(
    const std::vector<unsigned char>& grid, int w, int h,
    const std::vector<int>& px, const std::vector<int>& py,
    const std::vector<int>& cx, const std::vector<int>& cy,
    std::vector<float>* score, ScoreAllRunStats* stats);

// TEST8 module: TEST4 plus pinned host buffers and reusable device buffers.
bool score_all_gpu_block_cached_map_shared_pinned_reuse(
    const std::vector<unsigned char>& grid, int w, int h,
    const std::vector<int>& px, const std::vector<int>& py,
    const std::vector<int>& cx, const std::vector<int>& cy,
    std::vector<float>* score, ScoreAllRunStats* stats);

}  // namespace cartographer_parallel

#endif  // CARTOGRAPHER_PARALLEL_SCORE_ALL_BACKENDS_H_
