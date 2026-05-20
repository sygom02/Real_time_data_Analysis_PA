
#include "cartographer_parallel/score_all_backends.h"

#include <algorithm>

namespace cartographer_parallel {

void make_cand(const int min_x, const int max_x, const int min_y,
               const int max_y, const int step, std::vector<int>* const cx,
               std::vector<int>* const cy) {
  if (cx == nullptr || cy == nullptr || step <= 0) return;
  for (int x = min_x; x <= max_x; x += step) {
    for (int y = min_y; y <= max_y; y += step) {
      cx->push_back(x);
      cy->push_back(y);
    }
  }
}

void score_all_cpu_baseline(const std::vector<unsigned char>& grid, const int w,
                            const int h, const std::vector<int>& px,
                            const std::vector<int>& py,
                            const std::vector<int>& cx,
                            const std::vector<int>& cy,
                            std::vector<float>* const score,
                            ScoreAllRunStats* const stats) {
  if (score == nullptr) return;
  const int n = static_cast<int>(std::min(cx.size(), cy.size()));
  const int p = static_cast<int>(std::min(px.size(), py.size()));
  score->assign(n, 0.0f);
  if (stats != nullptr) {
    stats->input_candidates = n;
    stats->evaluated_candidates = n;
    stats->scan_points = p;
    stats->input_cell_checks =
        static_cast<long long>(n) * static_cast<long long>(p);
    stats->evaluated_cell_checks = stats->input_cell_checks;
    stats->cuda_requested = false;
    stats->cuda_available = false;
  }
  if (w <= 0 || h <= 0 || p == 0 || grid.size() < static_cast<size_t>(w * h)) {
    return;
  }

  for (int i = 0; i < n; ++i) {
    int sum = 0;
    for (int j = 0; j < p; ++j) {
      const int x = px[j] + cx[i];
      const int y = py[j] + cy[i];
      if (x >= 0 && x < w && y >= 0 && y < h) {
        sum += grid[y * w + x];
      }
    }
    (*score)[i] = static_cast<float>(sum) / (255.0f * static_cast<float>(p));
  }
}

}  // namespace cartographer_parallel
