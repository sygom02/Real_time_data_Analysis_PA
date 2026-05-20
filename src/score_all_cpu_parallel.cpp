#include "cartographer_parallel/score_all_backends.h"

#include <algorithm>
#include <thread>
#include <vector>

namespace cartographer_parallel {
namespace {

void ScoreCandidateRange(const std::vector<unsigned char>& grid, const int w,
                         const int h, const std::vector<int>& px,
                         const std::vector<int>& py,
                         const std::vector<int>& cx,
                         const std::vector<int>& cy, const int begin,
                         const int end, const int p,
                         std::vector<float>* const score) {
  for (int i = begin; i < end; ++i) {
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

}  // namespace

void score_all_cpu_parallel(const std::vector<unsigned char>& grid, const int w,
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
  if (w <= 0 || h <= 0 || n == 0 || p == 0 ||
      grid.size() < static_cast<size_t>(w * h)) {
    return;
  }

  const unsigned int hardware_threads = std::thread::hardware_concurrency();
  const int max_threads =
      hardware_threads > 0 ? static_cast<int>(hardware_threads) : 2;
  const int num_threads = std::max(1, std::min(max_threads, n));
  const int chunk = (n + num_threads - 1) / num_threads;

  std::vector<std::thread> workers;
  workers.reserve(num_threads);
  for (int t = 0; t < num_threads; ++t) {
    const int begin = t * chunk;
    const int end = std::min(n, begin + chunk);
    if (begin >= end) break;
    workers.emplace_back(ScoreCandidateRange, std::cref(grid), w, h,
                         std::cref(px), std::cref(py), std::cref(cx),
                         std::cref(cy), begin, end, p, score);
  }

  for (std::thread& worker : workers) {
    worker.join();
  }
}

}  // namespace cartographer_parallel
