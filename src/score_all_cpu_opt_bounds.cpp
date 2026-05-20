#include "cartographer_parallel/score_all_backends.h"

#include <algorithm>

namespace cartographer_parallel {
namespace {

void InitStats(const int n, const int p, ScoreAllRunStats* const stats) {
  if (stats == nullptr) return;
  stats->input_candidates = n;
  stats->evaluated_candidates = n;
  stats->scan_points = p;
  stats->input_cell_checks =
      static_cast<long long>(n) * static_cast<long long>(p);
  stats->evaluated_cell_checks = stats->input_cell_checks;
  stats->cuda_requested = false;
  stats->cuda_available = false;
}

void ScanBounds(const std::vector<int>& px, const std::vector<int>& py,
                const int p, int* const min_x, int* const max_x,
                int* const min_y, int* const max_y) {
  *min_x = px[0];
  *max_x = px[0];
  *min_y = py[0];
  *max_y = py[0];
  for (int j = 1; j < p; ++j) {
    *min_x = std::min(*min_x, px[j]);
    *max_x = std::max(*max_x, px[j]);
    *min_y = std::min(*min_y, py[j]);
    *max_y = std::max(*max_y, py[j]);
  }
}

}  // namespace

void score_all_cpu_opt_bounds(const std::vector<unsigned char>& grid,
                              const int w, const int h,
                              const std::vector<int>& px,
                              const std::vector<int>& py,
                              const std::vector<int>& cx,
                              const std::vector<int>& cy,
                              std::vector<float>* const score,
                              ScoreAllRunStats* const stats) {
  if (score == nullptr) return;
  const int n = static_cast<int>(std::min(cx.size(), cy.size()));
  const int p = static_cast<int>(std::min(px.size(), py.size()));
  InitStats(n, p, stats);

  score->resize(n);
  if (w <= 0 || h <= 0 || n == 0 || p == 0 ||
      grid.size() < static_cast<size_t>(w) * static_cast<size_t>(h)) {
    std::fill(score->begin(), score->end(), 0.0f);
    return;
  }

  int scan_min_x = 0;
  int scan_max_x = 0;
  int scan_min_y = 0;
  int scan_max_y = 0;
  ScanBounds(px, py, p, &scan_min_x, &scan_max_x, &scan_min_y, &scan_max_y);

  const unsigned char* const map = grid.data();
  const int* const scan_x = px.data();
  const int* const scan_y = py.data();
  const int* const cand_x = cx.data();
  const int* const cand_y = cy.data();
  const float inv_norm = 1.0f / (255.0f * static_cast<float>(p));

  for (int i = 0; i < n; ++i) {
    const int base_x = cand_x[i];
    const int base_y = cand_y[i];
    int sum = 0;
    const bool fully_inside =
        base_x + scan_min_x >= 0 && base_x + scan_max_x < w &&
        base_y + scan_min_y >= 0 && base_y + scan_max_y < h;

    if (fully_inside) {
      for (int j = 0; j < p; ++j) {
        const int x = base_x + scan_x[j];
        const int y = base_y + scan_y[j];
        sum += map[y * w + x];
      }
    } else {
      for (int j = 0; j < p; ++j) {
        const int x = base_x + scan_x[j];
        const int y = base_y + scan_y[j];
        if (static_cast<unsigned int>(x) < static_cast<unsigned int>(w) &&
            static_cast<unsigned int>(y) < static_cast<unsigned int>(h)) {
          sum += map[y * w + x];
        }
      }
    }
    (*score)[i] = static_cast<float>(sum) * inv_norm;
  }
}

}  // namespace cartographer_parallel
