#include "cartographer_parallel/score_all_backends.h"

namespace cartographer_parallel {

void score_all_cpu_opt_omp(const std::vector<unsigned char>& grid, const int w,
                           const int h, const std::vector<int>& px,
                           const std::vector<int>& py,
                           const std::vector<int>& cx,
                           const std::vector<int>& cy,
                           std::vector<float>* const score,
                           ScoreAllRunStats* const stats) {
  score_all_cpu_omp(grid, w, h, px, py, cx, cy, score, stats);
}

}  // namespace cartographer_parallel
