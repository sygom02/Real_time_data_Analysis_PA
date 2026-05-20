| step | score_all_ms | throughput_Mchecks_s | cpu_speedup_x | prev_ms_change_pct | eval_check_ratio_pct |
| --- | --- | --- | --- | --- | --- |
| test1_cpu_baseline | 41.820 | 102.681 | 1.000 | 0.00 | 100.00 |
| test2_gpu_thread_per_candidate | 14.366 | 298.944 | 2.911 | -65.65 | 100.00 |
| test3_gpu_block_per_candidate | 11.865 | 362.142 | 3.525 | -17.41 | 100.00 |
| test4_gpu_block_cached_map_shared | 4.839 | 888.067 | 8.642 | -59.22 | 100.00 |
| test5_gpu_block_cached_map_shared_pruned | 7.543 | 569.716 | 5.544 | 55.88 | 99.97 |
