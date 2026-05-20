#ifndef CARTOGRAPHER_PARALLEL_ASSIGNMENT_H_
#define CARTOGRAPHER_PARALLEL_ASSIGNMENT_H_

#include <string>
#include <vector>

namespace cartographer_parallel {

enum class ScoreAllBackend {
  kCpuBaseline,
  kCpuParallel,
  kCpuOptOffsets,
  kCpuOptBounds,
  kCpuOpt,
  kCpuOmp,
  kCpuOptOmp,
  kGpuThreadPerCandidate,
  kGpuThreadCachedMap,
  kGpuThreadPruned,
  kGpuBlockPerCandidate,
  kGpuBlockCachedMapShared,
  kGpuBlockCachedMapSharedPruned,
  kGpuBlockCachedMapSharedPinned,
  kGpuBlockCachedMapSharedReuseBuffers,
  kGpuBlockCachedMapSharedPinnedReuse,
};

struct ScoreAllRunStats {
  int input_candidates = 0;
  int evaluated_candidates = 0;
  int scan_points = 0;
  long long input_cell_checks = 0;
  long long evaluated_cell_checks = 0;
  bool cuda_requested = false;
  bool cuda_available = false;
};

void make_cand(int min_x, int max_x, int min_y, int max_y, int step,
               std::vector<int>* cx, std::vector<int>* cy);

void score_all(const std::vector<unsigned char>& grid, int w, int h,
               const std::vector<int>& px, const std::vector<int>& py,
               const std::vector<int>& cx, const std::vector<int>& cy,
               std::vector<float>* score);

bool set_score_all_backend(const std::string& backend);
ScoreAllBackend score_all_backend();
const char* score_all_backend_name();
std::string score_all_active_backend_name();
std::string score_all_backend_choices();
bool score_all_cuda_available();
void set_score_all_cuda_block_threads(int threads);
int score_all_cuda_block_threads();
ScoreAllRunStats last_score_all_run_stats();

}  // namespace cartographer_parallel

#endif  // CARTOGRAPHER_PARALLEL_ASSIGNMENT_H_
