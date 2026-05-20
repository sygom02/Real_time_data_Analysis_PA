#include "cartographer_parallel/score_all_backends.h"

#include <algorithm>

namespace cartographer_parallel {
namespace {

bool MarkCudaUnavailable(const std::vector<int>& px, const std::vector<int>& py,
                         const std::vector<int>& cx, const std::vector<int>& cy,
                         std::vector<float>* const score,
                         ScoreAllRunStats* const stats) {
  const int n = static_cast<int>(std::min(cx.size(), cy.size()));
  const int p = static_cast<int>(std::min(px.size(), py.size()));
  if (score != nullptr) score->assign(n, 0.0f);
  if (stats != nullptr) {
    stats->input_candidates = n;
    stats->evaluated_candidates = 0;
    stats->scan_points = p;
    stats->input_cell_checks =
        static_cast<long long>(n) * static_cast<long long>(p);
    stats->evaluated_cell_checks = 0;
    stats->cuda_requested = true;
    stats->cuda_available = false;
  }
  return false;
}

}  // namespace

bool score_all_gpu_thread_per_candidate(const std::vector<unsigned char>&, int,
                                        int, const std::vector<int>& px,
                                        const std::vector<int>& py,
                                        const std::vector<int>& cx,
                                        const std::vector<int>& cy,
                                        std::vector<float>* const score,
                                        ScoreAllRunStats* const stats) {
  // TEST2 module is compiled from .cu only when BUILD_CUDA_TASK=ON.
  return MarkCudaUnavailable(px, py, cx, cy, score, stats);
}

bool score_all_gpu_thread_cached_map(const std::vector<unsigned char>&, int,
                                     int, const std::vector<int>& px,
                                     const std::vector<int>& py,
                                     const std::vector<int>& cx,
                                     const std::vector<int>& cy,
                                     std::vector<float>* const score,
                                     ScoreAllRunStats* const stats) {
  // Isolated TEST2+TEST4 module is compiled only with CUDA enabled.
  return MarkCudaUnavailable(px, py, cx, cy, score, stats);
}

bool score_all_gpu_thread_pruned(const std::vector<unsigned char>&, int, int,
                                 const std::vector<int>& px,
                                 const std::vector<int>& py,
                                 const std::vector<int>& cx,
                                 const std::vector<int>& cy,
                                 std::vector<float>* const score,
                                 ScoreAllRunStats* const stats) {
  // Isolated TEST2+TEST5 module is compiled only with CUDA enabled.
  return MarkCudaUnavailable(px, py, cx, cy, score, stats);
}

bool score_all_gpu_block_per_candidate(const std::vector<unsigned char>&, int,
                                       int, const std::vector<int>& px,
                                       const std::vector<int>& py,
                                       const std::vector<int>& cx,
                                       const std::vector<int>& cy,
                                       std::vector<float>* const score,
                                       ScoreAllRunStats* const stats) {
  // TEST3 module is compiled from .cu only when BUILD_CUDA_TASK=ON.
  return MarkCudaUnavailable(px, py, cx, cy, score, stats);
}

bool score_all_gpu_block_cached_map_shared(
    const std::vector<unsigned char>&, int, int, const std::vector<int>& px,
    const std::vector<int>& py, const std::vector<int>& cx,
    const std::vector<int>& cy, std::vector<float>* const score,
    ScoreAllRunStats* const stats) {
  // TEST4 module is compiled from .cu only when BUILD_CUDA_TASK=ON.
  return MarkCudaUnavailable(px, py, cx, cy, score, stats);
}

bool score_all_gpu_block_cached_map_shared_pruned(
    const std::vector<unsigned char>&, int, int, const std::vector<int>& px,
    const std::vector<int>& py, const std::vector<int>& cx,
    const std::vector<int>& cy, std::vector<float>* const score,
    ScoreAllRunStats* const stats) {
  // TEST5 module is compiled from .cu only when BUILD_CUDA_TASK=ON.
  return MarkCudaUnavailable(px, py, cx, cy, score, stats);
}

bool score_all_gpu_block_cached_map_shared_pinned(
    const std::vector<unsigned char>&, int, int, const std::vector<int>& px,
    const std::vector<int>& py, const std::vector<int>& cx,
    const std::vector<int>& cy, std::vector<float>* const score,
    ScoreAllRunStats* const stats) {
  // TEST6 module is compiled from .cu only when BUILD_CUDA_TASK=ON.
  return MarkCudaUnavailable(px, py, cx, cy, score, stats);
}

}  // namespace cartographer_parallel
