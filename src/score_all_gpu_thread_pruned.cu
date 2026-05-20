#include "cartographer_parallel/score_all_backends.h"

#include <cuda_runtime.h>

#include <algorithm>
#include <vector>

namespace cartographer_parallel {
namespace {

__global__ void ScoreThreadPrunedKernel(
    const unsigned char* grid, const int w, const int h, const int* px,
    const int* py, const int p, const int* cx, const int* cy, const int n,
    float* score) {
  // Isolated TEST2+TEST5: keep one-thread-per-candidate, prune candidates only.
  const int i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i >= n) return;

  int sum = 0;
  for (int j = 0; j < p; ++j) {
    const int x = px[j] + cx[i];
    const int y = py[j] + cy[i];
    if (x >= 0 && x < w && y >= 0 && y < h) {
      sum += grid[y * w + x];
    }
  }
  score[i] = static_cast<float>(sum) / (255.0f * static_cast<float>(p));
}

template <typename T>
bool CopyVectorToDevice(const std::vector<T>& host, const int n, T** device) {
  *device = nullptr;
  if (n <= 0) return true;
  if (cudaMalloc(reinterpret_cast<void**>(device), sizeof(T) * n) !=
      cudaSuccess) {
    return false;
  }
  return cudaMemcpy(*device, host.data(), sizeof(T) * n,
                    cudaMemcpyHostToDevice) == cudaSuccess;
}

void FreeAll(const unsigned char* d_grid, const int* d_px, const int* d_py,
             const int* d_cx, const int* d_cy, const float* d_score) {
  cudaFree(const_cast<unsigned char*>(d_grid));
  cudaFree(const_cast<int*>(d_px));
  cudaFree(const_cast<int*>(d_py));
  cudaFree(const_cast<int*>(d_cx));
  cudaFree(const_cast<int*>(d_cy));
  cudaFree(const_cast<float*>(d_score));
}

void PruneCandidatesOutsideScanBounds(
    const std::vector<int>& px, const std::vector<int>& py,
    const std::vector<int>& cx, const std::vector<int>& cy, const int w,
    const int h, std::vector<int>* const keep_ids,
    std::vector<int>* const compact_cx, std::vector<int>* const compact_cy) {
  const auto px_minmax = std::minmax_element(px.begin(), px.end());
  const auto py_minmax = std::minmax_element(py.begin(), py.end());
  const int min_px = *px_minmax.first;
  const int max_px = *px_minmax.second;
  const int min_py = *py_minmax.first;
  const int max_py = *py_minmax.second;
  const int n = static_cast<int>(std::min(cx.size(), cy.size()));

  keep_ids->clear();
  compact_cx->clear();
  compact_cy->clear();
  keep_ids->reserve(n);
  compact_cx->reserve(n);
  compact_cy->reserve(n);
  for (int i = 0; i < n; ++i) {
    const bool outside_x = max_px + cx[i] < 0 || min_px + cx[i] >= w;
    const bool outside_y = max_py + cy[i] < 0 || min_py + cy[i] >= h;
    if (outside_x || outside_y) continue;
    keep_ids->push_back(i);
    compact_cx->push_back(cx[i]);
    compact_cy->push_back(cy[i]);
  }
}

}  // namespace

bool score_all_gpu_thread_pruned(
    const std::vector<unsigned char>& grid, const int w, const int h,
    const std::vector<int>& px, const std::vector<int>& py,
    const std::vector<int>& cx, const std::vector<int>& cy,
    std::vector<float>* const score, ScoreAllRunStats* const stats) {
  if (score == nullptr) return false;
  const int n = static_cast<int>(std::min(cx.size(), cy.size()));
  const int p = static_cast<int>(std::min(px.size(), py.size()));
  score->assign(n, 0.0f);
  if (n <= 0 || p <= 0 || w <= 0 || h <= 0 ||
      grid.size() < static_cast<size_t>(w * h)) {
    if (stats != nullptr) {
      stats->input_candidates = n;
      stats->evaluated_candidates = 0;
      stats->scan_points = p;
      stats->input_cell_checks =
          static_cast<long long>(n) * static_cast<long long>(p);
      stats->evaluated_cell_checks = 0;
      stats->cuda_requested = true;
      stats->cuda_available = true;
    }
    return true;
  }

  std::vector<int> keep_ids;
  std::vector<int> compact_cx;
  std::vector<int> compact_cy;
  PruneCandidatesOutsideScanBounds(px, py, cx, cy, w, h, &keep_ids,
                                   &compact_cx, &compact_cy);
  const int m = static_cast<int>(keep_ids.size());
  if (stats != nullptr) {
    stats->input_candidates = n;
    stats->evaluated_candidates = m;
    stats->scan_points = p;
    stats->input_cell_checks =
        static_cast<long long>(n) * static_cast<long long>(p);
    stats->evaluated_cell_checks =
        static_cast<long long>(m) * static_cast<long long>(p);
    stats->cuda_requested = true;
    stats->cuda_available = true;
  }
  if (m == 0) return true;

  unsigned char* d_grid = nullptr;
  int* d_px = nullptr;
  int* d_py = nullptr;
  int* d_cx = nullptr;
  int* d_cy = nullptr;
  float* d_score = nullptr;
  const size_t grid_bytes = sizeof(unsigned char) * grid.size();
  bool ok = cudaMalloc(reinterpret_cast<void**>(&d_grid), grid_bytes) ==
                cudaSuccess &&
            cudaMemcpy(d_grid, grid.data(), grid_bytes,
                       cudaMemcpyHostToDevice) == cudaSuccess &&
            CopyVectorToDevice(px, p, &d_px) &&
            CopyVectorToDevice(py, p, &d_py) &&
            CopyVectorToDevice(compact_cx, m, &d_cx) &&
            CopyVectorToDevice(compact_cy, m, &d_cy) &&
            cudaMalloc(reinterpret_cast<void**>(&d_score), sizeof(float) * m) ==
                cudaSuccess;
  if (!ok) {
    FreeAll(d_grid, d_px, d_py, d_cx, d_cy, d_score);
    return false;
  }

  std::vector<float> compact_score(m, 0.0f);
  const int threads = 256;
  const int blocks = (m + threads - 1) / threads;
  ScoreThreadPrunedKernel<<<blocks, threads>>>(d_grid, w, h, d_px, d_py, p,
                                               d_cx, d_cy, m, d_score);
  ok = cudaGetLastError() == cudaSuccess &&
       cudaDeviceSynchronize() == cudaSuccess &&
       cudaMemcpy(compact_score.data(), d_score, sizeof(float) * m,
                  cudaMemcpyDeviceToHost) == cudaSuccess;
  FreeAll(d_grid, d_px, d_py, d_cx, d_cy, d_score);
  if (!ok) return false;

  for (int i = 0; i < m; ++i) {
    (*score)[keep_ids[i]] = compact_score[i];
  }
  return true;
}

}  // namespace cartographer_parallel
