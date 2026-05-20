#include "cartographer_parallel/score_all_backends.h"

#include <algorithm>
#include <vector>

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

}  // namespace

void score_all_cpu_opt_offsets(const std::vector<unsigned char>& grid,
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

  static thread_local std::vector<int> scan_offsets;
  scan_offsets.resize(p);
  for (int j = 0; j < p; ++j) {
    scan_offsets[j] = py[j] * w + px[j];
  }

  const unsigned char* const map = grid.data();
  const int* const scan_x = px.data();
  const int* const scan_y = py.data();
  const int* const cand_x = cx.data();
  const int* const cand_y = cy.data();
  const int* const offsets = scan_offsets.data();
  const float inv_norm = 1.0f / (255.0f * static_cast<float>(p));

  for (int i = 0; i < n; ++i) {
    const int base_x = cand_x[i];
    const int base_y = cand_y[i];
    const int base_idx = base_y * w + base_x;
    int sum = 0;
    for (int j = 0; j < p; ++j) {
      const int x = base_x + scan_x[j];
      const int y = base_y + scan_y[j];
      if (x >= 0 && x < w && y >= 0 && y < h) {
        sum += map[base_idx + offsets[j]];
      }
    }
    (*score)[i] = static_cast<float>(sum) * inv_norm;
  }
}

}  // namespace cartographer_parallel
