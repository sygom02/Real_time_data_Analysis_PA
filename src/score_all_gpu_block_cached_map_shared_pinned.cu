#include "cartographer_parallel/score_all_backends.h"

#include <cuda_runtime.h>

#include <algorithm>
#include <vector>

namespace cartographer_parallel {
namespace {

const int kBlockThreads = 256;

struct CachedGrid {
  const unsigned char* host_ptr = nullptr;
  size_t size = 0;
  int w = 0;
  int h = 0;
  unsigned char* device_ptr = nullptr;
};

template <typename T>
struct PinnedBuffer {
  T* ptr = nullptr;
  size_t capacity = 0;
};

std::vector<CachedGrid> g_cached_grids;
PinnedBuffer<unsigned char> g_pinned_grid_upload;
PinnedBuffer<int> g_pinned_px;
PinnedBuffer<int> g_pinned_py;
PinnedBuffer<int> g_pinned_cx;
PinnedBuffer<int> g_pinned_cy;
PinnedBuffer<float> g_pinned_score;

__global__ void ScoreBlockCachedMapSharedPinnedKernel(
    const unsigned char* grid, const int w, const int h, const int* px,
    const int* py, const int p, const int* cx, const int* cy, const int n,
    float* score) {
  // TEST6: same block-per-candidate shared reduction as TEST4.
  const int i = blockIdx.x;
  if (i >= n) return;

  __shared__ int partial[kBlockThreads];
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
    if (threadIdx.x < stride) {
      partial[threadIdx.x] += partial[threadIdx.x + stride];
    }
    __syncthreads();
  }
  if (threadIdx.x == 0) {
    score[i] =
        static_cast<float>(partial[0]) / (255.0f * static_cast<float>(p));
  }
}

template <typename T>
bool EnsurePinnedCapacity(PinnedBuffer<T>* const buffer,
                          const size_t required) {
  if (buffer == nullptr) return false;
  if (required <= buffer->capacity) return true;

  T* next = nullptr;
  if (cudaMallocHost(reinterpret_cast<void**>(&next),
                     sizeof(T) * required) != cudaSuccess) {
    return false;
  }
  if (buffer->ptr != nullptr) cudaFreeHost(buffer->ptr);
  buffer->ptr = next;
  buffer->capacity = required;
  return true;
}

template <typename T>
bool CopyVectorToPinned(const std::vector<T>& host, const int n,
                        PinnedBuffer<T>* const pinned) {
  if (n <= 0) return true;
  if (!EnsurePinnedCapacity(pinned, static_cast<size_t>(n))) return false;
  std::copy(host.begin(), host.begin() + n, pinned->ptr);
  return true;
}

template <typename T>
bool CopyPinnedVectorToDevice(const std::vector<T>& host, const int n,
                              PinnedBuffer<T>* const pinned, T** device) {
  *device = nullptr;
  if (n <= 0) return true;
  if (!CopyVectorToPinned(host, n, pinned)) return false;
  if (cudaMalloc(reinterpret_cast<void**>(device), sizeof(T) * n) !=
      cudaSuccess) {
    return false;
  }
  return cudaMemcpy(*device, pinned->ptr, sizeof(T) * n,
                    cudaMemcpyHostToDevice) == cudaSuccess;
}

bool EnsureCachedGridPinned(const std::vector<unsigned char>& grid,
                            const int w, const int h,
                            unsigned char** const device_grid) {
  // TEST4 map cache: upload each grid level once.
  for (const CachedGrid& cached : g_cached_grids) {
    if (cached.host_ptr == grid.data() && cached.size == grid.size() &&
        cached.w == w && cached.h == h) {
      *device_grid = cached.device_ptr;
      return true;
    }
  }

  // TEST6 pinned memory: stage the one-time grid upload through pinned host RAM.
  if (!EnsurePinnedCapacity(&g_pinned_grid_upload, grid.size())) return false;
  std::copy(grid.begin(), grid.end(), g_pinned_grid_upload.ptr);

  unsigned char* uploaded = nullptr;
  const size_t bytes = sizeof(unsigned char) * grid.size();
  if (cudaMalloc(reinterpret_cast<void**>(&uploaded), bytes) != cudaSuccess) {
    return false;
  }
  if (cudaMemcpy(uploaded, g_pinned_grid_upload.ptr, bytes,
                 cudaMemcpyHostToDevice) != cudaSuccess) {
    cudaFree(uploaded);
    return false;
  }
  CachedGrid cached;
  cached.host_ptr = grid.data();
  cached.size = grid.size();
  cached.w = w;
  cached.h = h;
  cached.device_ptr = uploaded;
  g_cached_grids.push_back(cached);
  *device_grid = uploaded;
  return true;
}

void FreeCallBuffers(const int* d_px, const int* d_py, const int* d_cx,
                     const int* d_cy, const float* d_score) {
  cudaFree(const_cast<int*>(d_px));
  cudaFree(const_cast<int*>(d_py));
  cudaFree(const_cast<int*>(d_cx));
  cudaFree(const_cast<int*>(d_cy));
  cudaFree(const_cast<float*>(d_score));
}

}  // namespace

bool score_all_gpu_block_cached_map_shared_pinned(
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
  if (!EnsureCachedGridPinned(grid, w, h, &d_grid)) return false;

  int* d_px = nullptr;
  int* d_py = nullptr;
  int* d_cx = nullptr;
  int* d_cy = nullptr;
  float* d_score = nullptr;
  bool ok = CopyPinnedVectorToDevice(px, p, &g_pinned_px, &d_px) &&
            CopyPinnedVectorToDevice(py, p, &g_pinned_py, &d_py) &&
            CopyPinnedVectorToDevice(cx, n, &g_pinned_cx, &d_cx) &&
            CopyPinnedVectorToDevice(cy, n, &g_pinned_cy, &d_cy) &&
            EnsurePinnedCapacity(&g_pinned_score, static_cast<size_t>(n)) &&
            cudaMalloc(reinterpret_cast<void**>(&d_score), sizeof(float) * n) ==
                cudaSuccess;
  if (!ok) {
    FreeCallBuffers(d_px, d_py, d_cx, d_cy, d_score);
    return false;
  }

  ScoreBlockCachedMapSharedPinnedKernel<<<n, kBlockThreads>>>(
      d_grid, w, h, d_px, d_py, p, d_cx, d_cy, n, d_score);
  ok = cudaGetLastError() == cudaSuccess &&
       cudaDeviceSynchronize() == cudaSuccess &&
       cudaMemcpy(g_pinned_score.ptr, d_score, sizeof(float) * n,
                  cudaMemcpyDeviceToHost) == cudaSuccess;
  if (ok) {
    score->assign(g_pinned_score.ptr, g_pinned_score.ptr + n);
  }
  FreeCallBuffers(d_px, d_py, d_cx, d_cy, d_score);
  return ok;
}

}  // namespace cartographer_parallel
