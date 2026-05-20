# Real_time_data_Analysis_PA
PA_01

## 1. Build

```bash
cd /root/catkin_ws
source /opt/ros/melodic/setup.bash
catkin_make -DCMAKE_BUILD_TYPE=Release -DBUILD_CUDA_TASK=ON
source /root/catkin_ws/devel/setup.bash
```

패키지 루트:

```bash
cd /root/catkin_ws/src/cartographer_parallel/cartographer_parallel
mkdir -p results
```

## 2. Run
터미널 1에서 matcher node 실행:


```bash
cd /root/catkin_ws/src/cartographer_parallel/cartographer_parallel
source /opt/ros/melodic/setup.bash
source /root/catkin_ws/devel/setup.bash

roslaunch ./launch/cartographer_parallel_with_bag.launch \
  ns:=student02 \
  use_bag:=false \
  map_yaml_file:=$PWD/maps/0501.yaml \
  scan_topic:=scan \
  score_all_backend:=<BACKEND> \
  perf_csv_file:=$PWD/results/<CSV_FILE>
```

터미널 2에서 bag 실행:

```bash
cd /root/catkin_ws/src/cartographer_parallel/cartographer_parallel
source /opt/ros/melodic/setup.bash

rosbag play bags/scan.bag --clock /scan:=/student02/scan
```

실행 후 확인:

```bash
tail -1 results/<CSV_FILE>
```


## 3. CPU Tests

### CPU baseline

```bash
roslaunch ./launch/cartographer_parallel_with_bag.launch \
  ns:=student02 \
  use_bag:=false \
  map_yaml_file:=$PWD/maps/0501.yaml \
  scan_topic:=scan \
  score_all_backend:=cpu_baseline \
  perf_csv_file:=$PWD/results/cpu_baseline_perf.csv
```

### CPU parallel

```bash
roslaunch ./launch/cartographer_parallel_with_bag.launch \
  ns:=student02 \
  use_bag:=false \
  map_yaml_file:=$PWD/maps/0501.yaml \
  scan_topic:=scan \
  score_all_backend:=cpu_parallel \
  perf_csv_file:=$PWD/results/cpu_parallel_perf.csv
```

### CPU offset only

```bash
roslaunch ./launch/cartographer_parallel_with_bag.launch \
  ns:=student02 \
  use_bag:=false \
  map_yaml_file:=$PWD/maps/0501.yaml \
  scan_topic:=scan \
  score_all_backend:=cpu_opt_offsets \
  perf_csv_file:=$PWD/results/cpu_opt_offsets_perf.csv
```

### CPU bounds only

```bash
roslaunch ./launch/cartographer_parallel_with_bag.launch \
  ns:=student02 \
  use_bag:=false \
  map_yaml_file:=$PWD/maps/0501.yaml \
  scan_topic:=scan \
  score_all_backend:=cpu_opt_bounds \
  perf_csv_file:=$PWD/results/cpu_opt_bounds_perf.csv
```

### CPU optimized

```bash
roslaunch ./launch/cartographer_parallel_with_bag.launch \
  ns:=student02 \
  use_bag:=false \
  map_yaml_file:=$PWD/maps/0501.yaml \
  scan_topic:=scan \
  score_all_backend:=cpu_opt \
  perf_csv_file:=$PWD/results/cpu_opt_perf.csv
```

### CPU optimized + OpenMP

```bash
roslaunch ./launch/cartographer_parallel_with_bag.launch \
  ns:=student02 \
  use_bag:=false \
  map_yaml_file:=$PWD/maps/0501.yaml \
  scan_topic:=scan \
  score_all_backend:=cpu_opt_omp \
  perf_csv_file:=$PWD/results/cpu_opt_omp_perf.csv
```

## 4. GPU Tests

### TEST2: thread per candidate

```bash
roslaunch ./launch/cartographer_parallel_with_bag.launch \
  ns:=student02 \
  use_bag:=false \
  map_yaml_file:=$PWD/maps/0501.yaml \
  scan_topic:=scan \
  score_all_backend:=gpu_thread_per_candidate \
  perf_csv_file:=$PWD/results/test2_gpu_thread_per_candidate_perf.csv
```

### TEST3: block per candidate

```bash
roslaunch ./launch/cartographer_parallel_with_bag.launch \
  ns:=student02 \
  use_bag:=false \
  map_yaml_file:=$PWD/maps/0501.yaml \
  scan_topic:=scan \
  score_all_backend:=gpu_block_per_candidate \
  perf_csv_file:=$PWD/results/test3_gpu_block_per_candidate_perf.csv
```

### TEST4: cached map + shared memory

```bash
roslaunch ./launch/cartographer_parallel_with_bag.launch \
  ns:=student02 \
  use_bag:=false \
  map_yaml_file:=$PWD/maps/0501.yaml \
  scan_topic:=scan \
  score_all_backend:=gpu_block_cached_map_shared \
  perf_csv_file:=$PWD/results/test4_gpu_block_cached_map_shared_perf.csv
```


### TEST5: cached map + shared memory + pinned memory

```bash
roslaunch ./launch/cartographer_parallel_with_bag.launch \
  ns:=student02 \
  use_bag:=false \
  map_yaml_file:=$PWD/maps/0501.yaml \
  scan_topic:=scan \
  score_all_backend:=gpu_block_cached_map_shared_pinned \
  perf_csv_file:=$PWD/results/test6_gpu_block_cached_map_shared_pinned_perf.csv
```
### TEST6: cached map + shared memory + pruning

```bash
roslaunch ./launch/cartographer_parallel_with_bag.launch \
  ns:=student02 \
  use_bag:=false \
  map_yaml_file:=$PWD/maps/0501.yaml \
  scan_topic:=scan \
  score_all_backend:=gpu_block_cached_map_shared_pruned \
  perf_csv_file:=$PWD/results/test5_gpu_block_cached_map_shared_pruned_perf.csv
```

### TEST7: cached map + shared memory + reusable device buffers

```bash
roslaunch ./launch/cartographer_parallel_with_bag.launch \
  ns:=student02 \
  use_bag:=false \
  map_yaml_file:=$PWD/maps/0501.yaml \
  scan_topic:=scan \
  score_all_backend:=gpu_block_cached_map_shared_reuse_buffers \
  perf_csv_file:=$PWD/results/test7_shared_reuse_buffers_perf.csv
```



## 5. GPU Block Thread Tests

대상 backend:

```text
gpu_block_cached_map_shared
```

block thread 수 변경:

```bash
cd /root/catkin_ws/src/cartographer_parallel/cartographer_parallel

sed -i 's/const int kBlockThreads = [0-9]\+;/const int kBlockThreads = 64;/' \
  src/score_all_gpu_block_cached_map_shared.cu
```

값만 바꿔서 반복:

```text
64
128
256
512
1024
```

각 변경 후 다시 빌드:

```bash
cd /root/catkin_ws
source /opt/ros/melodic/setup.bash
catkin_make -DCMAKE_BUILD_TYPE=Release -DBUILD_CUDA_TASK=ON
source /root/catkin_ws/devel/setup.bash
```

실행 예:

```bash
cd /root/catkin_ws/src/cartographer_parallel/cartographer_parallel

roslaunch ./launch/cartographer_parallel_with_bag.launch \
  ns:=student02 \
  use_bag:=false \
  map_yaml_file:=$PWD/maps/0501.yaml \
  scan_topic:=scan \
  score_all_backend:=gpu_block_cached_map_shared \
  perf_csv_file:=$PWD/results/test4_shared_b64_perf.csv
```

CSV 파일명은 block thread 수에 맞춰 저장한다.

```text
test4_shared_b64_perf.csv
test4_shared_b128_perf.csv
test4_shared_b256_perf.csv
test4_shared_b512_perf.csv
test4_shared_b1024_perf.csv
```
