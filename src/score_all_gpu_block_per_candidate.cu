#include "cartographer_parallel/score_all_backends.h"

#include <cuda_runtime.h>

#include <algorithm>

namespace cartographer_parallel {
namespace {

const int kMaxBlockThreads = 1024;

__global__ void ScoreBlockPerCandidateKernel(
    const unsigned char* grid, const int w, const int h, const int* px,
    const int* py, const int p, const int* cx, const int* cy, const int n,
    float* score) {
  // TEST3: one CUDA block owns one candidate; threads split scan points.
  const int i = blockIdx.x;
  if (i >= n) return;

  __shared__ int partial[kMaxBlockThreads];
  int sum = 0;
  for (int j = threadIdx.x; j < p; j += blockDim.x) {
    const int x = px[j] + cx[i];
    const int y = py[j] + cy[i];
    if (x >= 0 && x < w && y >= 0 && y < h) {
      sum += grid[y * w + x];
    }
  }
  partial[threadIdx.x] = sum;
  __syncthreads();

  for (int stride = blockDim.x / 2; stride > 0; stride >>= 1) {
    if (threadIdx.x < stride) partial[threadIdx.x] += partial[threadIdx.x + stride];
    __syncthreads();
  }
  if (threadIdx.x == 0) {
    score[i] = static_cast<float>(partial[0]) /
               (255.0f * static_cast<float>(p));
  }
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

}  // namespace

bool score_all_gpu_block_per_candidate(
    const std::vector<unsigned char>& grid, const int w, const int h,
    const std::vector<int>& px, const std::vector<int>& py,
    const std::vector<int>& cx, const std::vector<int>& cy,
    std::vector<float>* const score, ScoreAllRunStats* const stats) {
  if (score == nullptr) return false;
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
    stats->cuda_requested = true;
    stats->cuda_available = true;
  }
  if (n <= 0 || p <= 0 || w <= 0 || h <= 0 ||
      grid.size() < static_cast<size_t>(w * h)) {
    return true;
  }

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
            CopyVectorToDevice(cx, n, &d_cx) &&
            CopyVectorToDevice(cy, n, &d_cy) &&
            cudaMalloc(reinterpret_cast<void**>(&d_score), sizeof(float) * n) ==
                cudaSuccess;
  if (!ok) {
    FreeAll(d_grid, d_px, d_py, d_cx, d_cy, d_score);
    return false;
  }

  const int threads = score_all_cuda_block_threads();
  ScoreBlockPerCandidateKernel<<<n, threads>>>(
      d_grid, w, h, d_px, d_py, p, d_cx, d_cy, n, d_score);
  ok = cudaGetLastError() == cudaSuccess &&
       cudaDeviceSynchronize() == cudaSuccess &&
       cudaMemcpy(score->data(), d_score, sizeof(float) * n,
                  cudaMemcpyDeviceToHost) == cudaSuccess;
  FreeAll(d_grid, d_px, d_py, d_cx, d_cy, d_score);
  return ok;
}

}  // namespace cartographer_parallel
