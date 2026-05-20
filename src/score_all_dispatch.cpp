#include "cartographer_parallel/assignment.h"

#include "cartographer_parallel/score_all_backends.h"

#include <algorithm>
#include <cctype>
#include <mutex>
#include <string>

namespace cartographer_parallel {
namespace {

std::mutex g_score_all_mutex;
ScoreAllBackend g_backend = ScoreAllBackend::kCpuBaseline;
ScoreAllRunStats g_last_stats;
std::string g_active_backend_name = "cpu_baseline";
int g_cuda_block_threads = 256;

int NormalizeCudaBlockThreads(const int threads) {
  if (threads <= 32) return 32;
  if (threads <= 64) return 64;
  if (threads <= 128) return 128;
  if (threads <= 256) return 256;
  if (threads <= 512) return 512;
  return 1024;
}

const char* BackendName(const ScoreAllBackend backend) {
  switch (backend) {
    case ScoreAllBackend::kCpuBaseline:
      return "cpu_baseline";
    case ScoreAllBackend::kCpuParallel:
      return "cpu_parallel";
    case ScoreAllBackend::kCpuOptOffsets:
      return "cpu_opt_offsets";
    case ScoreAllBackend::kCpuOptBounds:
      return "cpu_opt_bounds";
    case ScoreAllBackend::kCpuOpt:
      return "cpu_opt";
    case ScoreAllBackend::kCpuOmp:
      return "cpu_omp";
    case ScoreAllBackend::kCpuOptOmp:
      return "cpu_opt_omp";
    case ScoreAllBackend::kGpuThreadPerCandidate:
      return "gpu_thread_per_candidate";
    case ScoreAllBackend::kGpuThreadCachedMap:
      return "gpu_thread_cached_map";
    case ScoreAllBackend::kGpuThreadPruned:
      return "gpu_thread_pruned";
    case ScoreAllBackend::kGpuBlockPerCandidate:
      return "gpu_block_per_candidate";
    case ScoreAllBackend::kGpuBlockCachedMapShared:
      return "gpu_block_cached_map_shared";
    case ScoreAllBackend::kGpuBlockCachedMapSharedPruned:
      return "gpu_block_cached_map_shared_pruned";
    case ScoreAllBackend::kGpuBlockCachedMapSharedPinned:
      return "gpu_block_cached_map_shared_pinned";
    case ScoreAllBackend::kGpuBlockCachedMapSharedReuseBuffers:
      return "gpu_block_cached_map_shared_reuse_buffers";
    case ScoreAllBackend::kGpuBlockCachedMapSharedPinnedReuse:
      return "gpu_block_cached_map_shared_pinned_reuse";
  }
  return "cpu_baseline";
}

std::string NormalizeBackendName(std::string name) {
  std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  std::replace(name.begin(), name.end(), '-', '_');
  return name;
}

bool ParseBackend(const std::string& name, ScoreAllBackend* const backend) {
  if (backend == nullptr) return false;
  const std::string n = NormalizeBackendName(name);
  if (n == "cpu" || n == "cpu_baseline") {
    *backend = ScoreAllBackend::kCpuBaseline;
    return true;
  }
  if (n == "cpu_parallel" || n == "cpu_threads" || n == "cpu_thread" ||
      n == "cpu_multithread") {
    *backend = ScoreAllBackend::kCpuParallel;
    return true;
  }
  if (n == "cpu_opt_offsets" || n == "cpu_offsets" ||
      n == "cpu_offset") {
    *backend = ScoreAllBackend::kCpuOptOffsets;
    return true;
  }
  if (n == "cpu_opt_bounds" || n == "cpu_bounds" ||
      n == "cpu_boundary") {
    *backend = ScoreAllBackend::kCpuOptBounds;
    return true;
  }
  if (n == "cpu_opt" || n == "cpu_optimized") {
    *backend = ScoreAllBackend::kCpuOpt;
    return true;
  }
  if (n == "cpu_omp" || n == "cpu_openmp") {
    *backend = ScoreAllBackend::kCpuOmp;
    return true;
  }
  if (n == "cpu_opt_omp" || n == "cpu_omp_opt" ||
      n == "cpu_opt_parallel" || n == "cpu_parallel_opt") {
    *backend = ScoreAllBackend::kCpuOptOmp;
    return true;
  }
  if (n == "test2" || n == "gpu_thread_per_candidate") {
    *backend = ScoreAllBackend::kGpuThreadPerCandidate;
    return true;
  }
  if (n == "test2_test4" || n == "test2_plus_test4" ||
      n == "thread_cached_map" || n == "gpu_thread_cached_map") {
    *backend = ScoreAllBackend::kGpuThreadCachedMap;
    return true;
  }
  if (n == "test2_test5" || n == "test2_plus_test5" ||
      n == "thread_pruned" || n == "gpu_thread_pruned") {
    *backend = ScoreAllBackend::kGpuThreadPruned;
    return true;
  }
  if (n == "test3" || n == "gpu_block_per_candidate") {
    *backend = ScoreAllBackend::kGpuBlockPerCandidate;
    return true;
  }
  if (n == "test4" || n == "gpu_block_cached_map_shared") {
    *backend = ScoreAllBackend::kGpuBlockCachedMapShared;
    return true;
  }
  if (n == "test5" || n == "gpu_block_cached_map_shared_pruned") {
    *backend = ScoreAllBackend::kGpuBlockCachedMapSharedPruned;
    return true;
  }
  if (n == "test6" || n == "pinned" ||
      n == "gpu_block_cached_map_shared_pinned") {
    *backend = ScoreAllBackend::kGpuBlockCachedMapSharedPinned;
    return true;
  }
  if (n == "test7" || n == "reuse" || n == "reuse_buffers" ||
      n == "gpu_block_cached_map_shared_reuse_buffers") {
    *backend = ScoreAllBackend::kGpuBlockCachedMapSharedReuseBuffers;
    return true;
  }
  if (n == "test8" || n == "pinned_reuse" ||
      n == "gpu_block_cached_map_shared_pinned_reuse") {
    *backend = ScoreAllBackend::kGpuBlockCachedMapSharedPinnedReuse;
    return true;
  }
  return false;
}

void StoreLastStats(const ScoreAllRunStats& stats,
                    const std::string& active_backend_name) {
  std::lock_guard<std::mutex> lock(g_score_all_mutex);
  g_last_stats = stats;
  g_active_backend_name = active_backend_name;
}

}  // namespace

bool set_score_all_backend(const std::string& backend) {
  ScoreAllBackend parsed;
  if (!ParseBackend(backend, &parsed)) return false;
  std::lock_guard<std::mutex> lock(g_score_all_mutex);
  g_backend = parsed;
  g_active_backend_name = BackendName(parsed);
  return true;
}

ScoreAllBackend score_all_backend() {
  std::lock_guard<std::mutex> lock(g_score_all_mutex);
  return g_backend;
}

const char* score_all_backend_name() { return BackendName(score_all_backend()); }

std::string score_all_active_backend_name() {
  std::lock_guard<std::mutex> lock(g_score_all_mutex);
  return g_active_backend_name;
}

std::string score_all_backend_choices() {
  return "cpu_baseline, cpu_parallel, cpu_opt_offsets, cpu_opt_bounds, "
         "cpu_opt, cpu_omp, cpu_opt_omp, gpu_thread_per_candidate, "
         "gpu_thread_cached_map, gpu_thread_pruned, gpu_block_per_candidate, "
         "gpu_block_cached_map_shared, gpu_block_cached_map_shared_pruned, "
         "gpu_block_cached_map_shared_pinned, "
         "gpu_block_cached_map_shared_reuse_buffers, "
         "gpu_block_cached_map_shared_pinned_reuse";
}

bool score_all_cuda_available() {
#ifdef CARTOGRAPHER_PARALLEL_CUDA_BUILD
  return true;
#else
  return false;
#endif
}

void set_score_all_cuda_block_threads(const int threads) {
  std::lock_guard<std::mutex> lock(g_score_all_mutex);
  g_cuda_block_threads = NormalizeCudaBlockThreads(threads);
}

int score_all_cuda_block_threads() {
  std::lock_guard<std::mutex> lock(g_score_all_mutex);
  return g_cuda_block_threads;
}

ScoreAllRunStats last_score_all_run_stats() {
  std::lock_guard<std::mutex> lock(g_score_all_mutex);
  return g_last_stats;
}

void score_all(const std::vector<unsigned char>& grid, const int w,
               const int h, const std::vector<int>& px,
               const std::vector<int>& py, const std::vector<int>& cx,
               const std::vector<int>& cy, std::vector<float>* const score) {
  const ScoreAllBackend backend = score_all_backend();
  ScoreAllRunStats stats;
  bool ran_requested_backend = false;

  // Module selector: only score_all() changes between experiments.
  switch (backend) {
    case ScoreAllBackend::kCpuBaseline:
      score_all_cpu_baseline(grid, w, h, px, py, cx, cy, score, &stats);
      StoreLastStats(stats, "cpu_baseline");
      return;
    case ScoreAllBackend::kCpuParallel:
      score_all_cpu_parallel(grid, w, h, px, py, cx, cy, score, &stats);
      StoreLastStats(stats, "cpu_parallel");
      return;
    case ScoreAllBackend::kCpuOptOffsets:
      score_all_cpu_opt_offsets(grid, w, h, px, py, cx, cy, score, &stats);
      StoreLastStats(stats, "cpu_opt_offsets");
      return;
    case ScoreAllBackend::kCpuOptBounds:
      score_all_cpu_opt_bounds(grid, w, h, px, py, cx, cy, score, &stats);
      StoreLastStats(stats, "cpu_opt_bounds");
      return;
    case ScoreAllBackend::kCpuOpt:
      score_all_cpu_opt(grid, w, h, px, py, cx, cy, score, &stats);
      StoreLastStats(stats, "cpu_opt");
      return;
    case ScoreAllBackend::kCpuOmp:
      score_all_cpu_omp(grid, w, h, px, py, cx, cy, score, &stats);
      StoreLastStats(stats, "cpu_omp");
      return;
    case ScoreAllBackend::kCpuOptOmp:
      score_all_cpu_opt_omp(grid, w, h, px, py, cx, cy, score, &stats);
      StoreLastStats(stats, "cpu_opt_omp");
      return;
    case ScoreAllBackend::kGpuThreadPerCandidate:
      ran_requested_backend = score_all_gpu_thread_per_candidate(
          grid, w, h, px, py, cx, cy, score, &stats);
      break;
    case ScoreAllBackend::kGpuThreadCachedMap:
      ran_requested_backend = score_all_gpu_thread_cached_map(
          grid, w, h, px, py, cx, cy, score, &stats);
      break;
    case ScoreAllBackend::kGpuThreadPruned:
      ran_requested_backend = score_all_gpu_thread_pruned(
          grid, w, h, px, py, cx, cy, score, &stats);
      break;
    case ScoreAllBackend::kGpuBlockPerCandidate:
      ran_requested_backend = score_all_gpu_block_per_candidate(
          grid, w, h, px, py, cx, cy, score, &stats);
      break;
    case ScoreAllBackend::kGpuBlockCachedMapShared:
      ran_requested_backend = score_all_gpu_block_cached_map_shared(
          grid, w, h, px, py, cx, cy, score, &stats);
      break;
    case ScoreAllBackend::kGpuBlockCachedMapSharedPruned:
      ran_requested_backend = score_all_gpu_block_cached_map_shared_pruned(
          grid, w, h, px, py, cx, cy, score, &stats);
      break;
    case ScoreAllBackend::kGpuBlockCachedMapSharedPinned:
      ran_requested_backend = score_all_gpu_block_cached_map_shared_pinned(
          grid, w, h, px, py, cx, cy, score, &stats);
      break;
    case ScoreAllBackend::kGpuBlockCachedMapSharedReuseBuffers:
      ran_requested_backend =
          score_all_gpu_block_cached_map_shared_reuse_buffers(
              grid, w, h, px, py, cx, cy, score, &stats);
      break;
    case ScoreAllBackend::kGpuBlockCachedMapSharedPinnedReuse:
      ran_requested_backend =
          score_all_gpu_block_cached_map_shared_pinned_reuse(
              grid, w, h, px, py, cx, cy, score, &stats);
      break;
  }

  if (ran_requested_backend) {
    stats.cuda_requested = true;
    stats.cuda_available = true;
    StoreLastStats(stats, BackendName(backend));
    return;
  }

  // CUDA fallback: keeps the node usable when it is launched with a GPU
  // backend but was built on a machine without nvcc/CUDA runtime.
  score_all_cpu_baseline(grid, w, h, px, py, cx, cy, score, &stats);
  stats.cuda_requested = true;
  stats.cuda_available = false;
  StoreLastStats(stats, "cpu_fallback");
}

}  // namespace cartographer_parallel
