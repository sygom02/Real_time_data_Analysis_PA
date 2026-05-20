#include "cartographer_parallel/score_all_backends.h"

#include <cuda_runtime.h>

#include <algorithm>
#include <vector>

namespace cartographer_parallel {
namespace {

const int kMaxBlockThreads = 1024;

struct CachedGrid {
  const unsigned char* host_ptr = nullptr;
  size_t size = 0;
  int w = 0;
  int h = 0;
  unsigned char* device_ptr = nullptr;
};

template <typename T>
struct DeviceBuffer {
  T* ptr = nullptr;
  size_t capacity = 0;
};

template <typename T>
struct PinnedBuffer {
  T* ptr = nullptr;
  size_t capacity = 0;
};

struct ReusableDeviceBuffers {
  DeviceBuffer<int> px;
  DeviceBuffer<int> py;
  DeviceBuffer<int> cx;
  DeviceBuffer<int> cy;
  DeviceBuffer<float> score;
};

struct ReusablePinnedBuffers {
  PinnedBuffer<int> px;
  PinnedBuffer<int> py;
  PinnedBuffer<int> cx;
  PinnedBuffer<int> cy;
  PinnedBuffer<float> score;
};

std::vector<CachedGrid> g_cached_grids_reuse;
std::vector<CachedGrid> g_cached_grids_pinned_reuse;
ReusableDeviceBuffers g_reuse_device;
ReusableDeviceBuffers g_pinned_reuse_device;
ReusablePinnedBuffers g_pinned_reuse_host;

__global__ void ScoreBlockCachedMapSharedReuseKernel(
    const unsigned char* grid, const int w, const int h, const int* px,
    const int* py, const int p, const int* cx, const int* cy, const int n,
    float* score) {
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

bool EnsureCachedGrid(std::vector<CachedGrid>* const cached_grids,
                      const std::vector<unsigned char>& grid, const int w,
                      const int h, unsigned char** const device_grid) {
  for (const CachedGrid& cached : *cached_grids) {
    if (cached.host_ptr == grid.data() && cached.size == grid.size() &&
        cached.w == w && cached.h == h) {
      *device_grid = cached.device_ptr;
      return true;
    }
  }

  unsigned char* uploaded = nullptr;
  const size_t bytes = sizeof(unsigned char) * grid.size();
  if (cudaMalloc(reinterpret_cast<void**>(&uploaded), bytes) != cudaSuccess) {
    return false;
  }
  if (cudaMemcpy(uploaded, grid.data(), bytes, cudaMemcpyHostToDevice) !=
      cudaSuccess) {
    cudaFree(uploaded);
    return false;
  }

  CachedGrid cached;
  cached.host_ptr = grid.data();
  cached.size = grid.size();
  cached.w = w;
  cached.h = h;
  cached.device_ptr = uploaded;
  cached_grids->push_back(cached);
  *device_grid = uploaded;
  return true;
}

template <typename T>
bool EnsureDeviceCapacity(DeviceBuffer<T>* const buffer,
                          const size_t required) {
  if (buffer == nullptr) return false;
  if (required <= buffer->capacity) return true;

  T* next = nullptr;
  if (cudaMalloc(reinterpret_cast<void**>(&next), sizeof(T) * required) !=
      cudaSuccess) {
    return false;
  }
  if (buffer->ptr != nullptr) cudaFree(buffer->ptr);
  buffer->ptr = next;
  buffer->capacity = required;
  return true;
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
bool CopyVectorToDeviceReuse(const std::vector<T>& host, const int n,
                             DeviceBuffer<T>* const device) {
  if (n <= 0) return true;
  if (!EnsureDeviceCapacity(device, static_cast<size_t>(n))) return false;
  return cudaMemcpy(device->ptr, host.data(), sizeof(T) * n,
                    cudaMemcpyHostToDevice) == cudaSuccess;
}

template <typename T>
bool CopyVectorToPinnedAndDeviceReuse(const std::vector<T>& host, const int n,
                                      PinnedBuffer<T>* const pinned,
                                      DeviceBuffer<T>* const device) {
  if (n <= 0) return true;
  if (!EnsurePinnedCapacity(pinned, static_cast<size_t>(n))) return false;
  if (!EnsureDeviceCapacity(device, static_cast<size_t>(n))) return false;
  std::copy(host.begin(), host.begin() + n, pinned->ptr);
  return cudaMemcpy(device->ptr, pinned->ptr, sizeof(T) * n,
                    cudaMemcpyHostToDevice) == cudaSuccess;
}

void InitStats(const int n, const int p, ScoreAllRunStats* const stats) {
  if (stats == nullptr) return;
  stats->input_candidates = n;
  stats->evaluated_candidates = n;
  stats->scan_points = p;
  stats->input_cell_checks =
      static_cast<long long>(n) * static_cast<long long>(p);
  stats->evaluated_cell_checks = stats->input_cell_checks;
  stats->cuda_requested = true;
  stats->cuda_available = true;
}

bool ValidInput(const std::vector<unsigned char>& grid, const int w,
                const int h, const int n, const int p) {
  return n > 0 && p > 0 && w > 0 && h > 0 &&
         grid.size() >= static_cast<size_t>(w) * static_cast<size_t>(h);
}

}  // namespace

bool score_all_gpu_block_cached_map_shared_reuse_buffers(
    const std::vector<unsigned char>& grid, const int w, const int h,
    const std::vector<int>& px, const std::vector<int>& py,
    const std::vector<int>& cx, const std::vector<int>& cy,
    std::vector<float>* const score, ScoreAllRunStats* const stats) {
  if (score == nullptr) return false;
  const int n = static_cast<int>(std::min(cx.size(), cy.size()));
  const int p = static_cast<int>(std::min(px.size(), py.size()));
  score->assign(n, 0.0f);
  InitStats(n, p, stats);
  if (!ValidInput(grid, w, h, n, p)) return true;

  unsigned char* d_grid = nullptr;
  if (!EnsureCachedGrid(&g_cached_grids_reuse, grid, w, h, &d_grid)) {
    return false;
  }

  const bool ok_copy =
      CopyVectorToDeviceReuse(px, p, &g_reuse_device.px) &&
      CopyVectorToDeviceReuse(py, p, &g_reuse_device.py) &&
      CopyVectorToDeviceReuse(cx, n, &g_reuse_device.cx) &&
      CopyVectorToDeviceReuse(cy, n, &g_reuse_device.cy) &&
      EnsureDeviceCapacity(&g_reuse_device.score, static_cast<size_t>(n));
  if (!ok_copy) return false;

  const int threads = score_all_cuda_block_threads();
  ScoreBlockCachedMapSharedReuseKernel<<<n, threads>>>(
      d_grid, w, h, g_reuse_device.px.ptr, g_reuse_device.py.ptr, p,
      g_reuse_device.cx.ptr, g_reuse_device.cy.ptr, n,
      g_reuse_device.score.ptr);

  return cudaGetLastError() == cudaSuccess &&
         cudaDeviceSynchronize() == cudaSuccess &&
         cudaMemcpy(score->data(), g_reuse_device.score.ptr,
                    sizeof(float) * n, cudaMemcpyDeviceToHost) == cudaSuccess;
}

bool score_all_gpu_block_cached_map_shared_pinned_reuse(
    const std::vector<unsigned char>& grid, const int w, const int h,
    const std::vector<int>& px, const std::vector<int>& py,
    const std::vector<int>& cx, const std::vector<int>& cy,
    std::vector<float>* const score, ScoreAllRunStats* const stats) {
  if (score == nullptr) return false;
  const int n = static_cast<int>(std::min(cx.size(), cy.size()));
  const int p = static_cast<int>(std::min(px.size(), py.size()));
  score->assign(n, 0.0f);
  InitStats(n, p, stats);
  if (!ValidInput(grid, w, h, n, p)) return true;

  unsigned char* d_grid = nullptr;
  if (!EnsureCachedGrid(&g_cached_grids_pinned_reuse, grid, w, h, &d_grid)) {
    return false;
  }

  const bool ok_copy =
      CopyVectorToPinnedAndDeviceReuse(px, p, &g_pinned_reuse_host.px,
                                       &g_pinned_reuse_device.px) &&
      CopyVectorToPinnedAndDeviceReuse(py, p, &g_pinned_reuse_host.py,
                                       &g_pinned_reuse_device.py) &&
      CopyVectorToPinnedAndDeviceReuse(cx, n, &g_pinned_reuse_host.cx,
                                       &g_pinned_reuse_device.cx) &&
      CopyVectorToPinnedAndDeviceReuse(cy, n, &g_pinned_reuse_host.cy,
                                       &g_pinned_reuse_device.cy) &&
      EnsureDeviceCapacity(&g_pinned_reuse_device.score,
                           static_cast<size_t>(n)) &&
      EnsurePinnedCapacity(&g_pinned_reuse_host.score,
                           static_cast<size_t>(n));
  if (!ok_copy) return false;

  const int threads = score_all_cuda_block_threads();
  ScoreBlockCachedMapSharedReuseKernel<<<n, threads>>>(
      d_grid, w, h, g_pinned_reuse_device.px.ptr,
      g_pinned_reuse_device.py.ptr, p, g_pinned_reuse_device.cx.ptr,
      g_pinned_reuse_device.cy.ptr, n, g_pinned_reuse_device.score.ptr);

  const bool ok = cudaGetLastError() == cudaSuccess &&
                  cudaDeviceSynchronize() == cudaSuccess &&
                  cudaMemcpy(g_pinned_reuse_host.score.ptr,
                             g_pinned_reuse_device.score.ptr,
                             sizeof(float) * n,
                             cudaMemcpyDeviceToHost) == cudaSuccess;
  if (ok) {
    score->assign(g_pinned_reuse_host.score.ptr,
                  g_pinned_reuse_host.score.ptr + n);
  }
  return ok;
}

}  // namespace cartographer_parallel
