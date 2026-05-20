# 결과 분석 요약

이 문서는 `results/` 아래에 생성된 성능 측정 결과를 보기 쉽게 정리한 것입니다.

## 한 줄 결론

현재 실험 결과 기준으로는 **TEST4 `gpu_block_cached_map_shared`가 가장 좋습니다.**

- `score_all_ms`: **4.839 ms**
- throughput: **888.067 Mchecks/s**
- CPU baseline 대비: **8.64x faster**
- 이전 단계 TEST3 대비 `score_all_ms`: **59.22% 감소**

TEST5 pruning은 오히려 느려졌습니다. pruning으로 실제 제거된 check가 거의 없어서 CPU pruning 비용이 더 크게 나온 것으로 해석됩니다.

## 어떤 결과를 보면 되나

전체 TEST1~TEST5 비교는 아래 파일을 보면 됩니다.

```bash
cat results/all_tests/perf_analysis_table.md
```

그래프는 아래 파일입니다.

- [score_all_ms_avg.png](all_tests/score_all_ms_avg.png): 낮을수록 좋음
- [throughput_avg.png](all_tests/throughput_avg.png): 높을수록 좋음
- [cpu_speedup.png](all_tests/cpu_speedup.png): 높을수록 좋음

원본 CSV는 아래 파일입니다.

- [perf_runs_all.csv](perf_runs_all.csv): TEST1~TEST5 전체 실행 결과
- [all_tests/perf_analysis_summary.csv](all_tests/perf_analysis_summary.csv): 분석용 요약 CSV
- [final_experiment_summary_README.md](final_experiment_summary_README.md): 누적/개별/pinned 실험 통합 요약
- [module_improvement_README.md](module_improvement_README.md): TEST2 기준 개별 모듈 적용 실험

## 비교할 때 봐야 하는 것

가장 중요한 지표는 `score_all_ms`입니다. 이번 과제의 목표가 전체 Cartographer를 바꾸는 것이 아니라 `score_all()` 계산 부분을 CPU/GPU 버전으로 바꾸어 비교하는 것이기 때문입니다.

우선순위는 다음 순서로 보면 됩니다.

| 우선순위 | 지표 | 의미 | 해석 |
| --- | --- | --- | --- |
| 1 | `score_all_ms` | `score_all()` 계산 시간 | 낮을수록 좋음 |
| 2 | `score_all_throughput_mchecks_s_avg` | 초당 candidate-point check 수 | 높을수록 좋음 |
| 3 | `cpu_speedup_x` | CPU baseline 대비 몇 배 빠른지 | 높을수록 좋음 |
| 4 | `prev_score_all_ms_change_pct` | 바로 이전 TEST 대비 시간 변화율 | 음수면 개선, 양수면 악화 |
| 5 | `match_ms_avg` | scan matching 전체 시간 | 참고용, 낮을수록 좋음 |
| 6 | `evaluated_cell_check_ratio_pct` | 실제 평가한 cell check 비율 | pruning 효과 확인용 |

보고서에서는 보통 아래 순서로 비교하면 됩니다.

1. **TEST1 -> TEST2**: CPU loop를 GPU thread 병렬화로 옮겼을 때 빨라졌는가?
2. **TEST2 -> TEST3**: candidate 하나를 thread 하나가 처리하는 방식보다 block 하나가 처리하는 방식이 좋은가?
3. **TEST3 -> TEST4**: map cache와 shared memory reduction이 효과가 있는가?
4. **TEST4 -> TEST5**: pruning으로 줄인 계산량이 pruning overhead보다 큰가?
5. **TEST4 -> TEST4+pinned**: pinned memory가 host-device transfer 비용을 줄였는가?

주의할 점:

- `score_all_ms`는 이번 실험의 핵심 지표입니다.
- `match_ms_avg`는 전체 matching 시간이라 ROS publish, 후보 생성, branch-and-bound 흐름 등의 영향도 같이 들어갑니다.
- TEST5는 pruning 실험이므로 `score_all_ms`만 보면 안 되고 `evaluated_cell_check_ratio_pct`를 반드시 같이 봐야 합니다.
- pinned memory 실험은 `results/block_ablation_pinned/`에서 따로 다시 실행한 보조 실험이므로, 같은 표 안에서 TEST4와 TEST4+pinned를 비교하는 것이 맞습니다.
- 현재 각 TEST는 `runs=1`이라 반복 평균은 아닙니다. 더 엄밀하게 보려면 같은 TEST를 여러 번 실행해서 `runs`를 늘린 뒤 평균을 비교해야 합니다.

## 각 실험이 확인하는 것

### TEST1: CPU baseline

| 항목 | 내용 |
| --- | --- |
| backend | `cpu_baseline` |
| 목적 | GPU 최적화 전 기준 성능 측정 |
| 계산 방식 | CPU에서 candidate를 하나씩 순회하고, 각 candidate마다 scan point 전체를 순회 |
| 비교 대상 | 이후 모든 GPU 실험의 기준 |
| 봐야 할 지표 | `score_all_ms`, `cpu_speedup_x=1.00` |

TEST1은 기준선입니다. 이 값보다 GPU 버전의 `score_all_ms`가 얼마나 줄었는지를 봅니다. 이번 결과에서는 CPU baseline의 `score_all_ms`가 **41.820 ms**입니다.

### TEST2: GPU thread-per-candidate

| 항목 | 내용 |
| --- | --- |
| backend | `gpu_thread_per_candidate` |
| 목적 | 가장 단순한 GPU 병렬화가 CPU보다 빠른지 확인 |
| 계산 방식 | CUDA thread 1개가 candidate 1개를 맡고, 그 candidate의 scan point들을 순차 합산 |
| 추가된 것 | candidate 단위 GPU 병렬화 |
| 비교 대상 | TEST1 CPU baseline |
| 봐야 할 지표 | `score_all_ms`, `throughput`, `cpu_speedup_x` |

TEST2는 CPU의 candidate loop를 GPU kernel로 옮긴 첫 번째 버전입니다. candidate 수만큼 thread를 만들어 동시에 처리하지만, candidate 하나 내부의 scan point 합산은 thread 하나가 혼자 합니다. 그래서 CPU보다는 빨라질 수 있지만, scan point가 많으면 thread 하나에 일이 몰립니다.

이번 결과에서는 TEST1 대비 `score_all_ms`가 **41.820 ms -> 14.366 ms**로 줄었고, CPU 대비 **2.91x** 빨라졌습니다.

### TEST3: GPU block-per-candidate

| 항목 | 내용 |
| --- | --- |
| backend | `gpu_block_per_candidate` |
| 목적 | candidate 내부 scan point 합산까지 병렬화하면 더 빨라지는지 확인 |
| 계산 방식 | CUDA block 1개가 candidate 1개를 맡고, block 안의 thread들이 scan point를 나눠 계산 |
| 추가된 것 | block-per-candidate 구조, shared memory reduction |
| 비교 대상 | TEST2 |
| 봐야 할 지표 | `score_all_ms`, `throughput`, `prev_score_all_ms_change_pct` |

TEST3는 candidate 하나를 thread 하나가 처리하던 TEST2와 달리, block 하나가 candidate 하나를 처리합니다. block 안의 여러 thread가 scan point들을 나눠서 더하고, 마지막에 shared memory로 partial sum을 합칩니다.

이번 결과에서는 TEST2 대비 `score_all_ms`가 **14.366 ms -> 11.865 ms**로 줄었습니다. 개선 폭은 **17.41%**입니다. 즉, candidate 내부 합산 병렬화가 추가 성능 개선을 만들었습니다.

### TEST4: GPU block + cached map + shared memory

| 항목 | 내용 |
| --- | --- |
| backend | `gpu_block_cached_map_shared` |
| 목적 | map grid를 매번 GPU로 복사하지 않고 cache하면 얼마나 빨라지는지 확인 |
| 계산 방식 | TEST3의 block-per-candidate 구조를 유지하고, map grid는 GPU memory에 cache해 재사용 |
| 추가된 것 | map GPU cache, shared memory reduction 유지 |
| 비교 대상 | TEST3 |
| 봐야 할 지표 | `score_all_ms`, `throughput`, `prev_score_all_ms_change_pct` |

TEST4는 이번 실험에서 가장 중요한 최적화입니다. `score_all()`은 같은 map grid를 반복해서 사용하므로, 매 호출마다 map을 GPU로 복사하면 transfer 비용이 커집니다. TEST4는 map을 GPU에 한 번 올린 뒤 재사용하고, 호출마다 바뀌는 candidate/scan 관련 데이터만 전송합니다.

이번 결과에서는 TEST3 대비 `score_all_ms`가 **11.865 ms -> 4.839 ms**로 줄었습니다. 개선 폭은 **59.22%**이고, CPU 대비 **8.64x** 빨라졌습니다. 따라서 현재 메인 실험의 최종 선택은 TEST4가 가장 타당합니다.

### TEST5: TEST4 + pruning

| 항목 | 내용 |
| --- | --- |
| backend | `gpu_block_cached_map_shared_pruned` |
| 목적 | GPU로 보내기 전에 불필요한 candidate를 CPU에서 제거하면 빨라지는지 확인 |
| 계산 방식 | scan bounding box가 map 밖으로 완전히 나가는 candidate를 제거한 뒤 GPU kernel 실행 |
| 추가된 것 | CPU-side candidate pruning |
| 비교 대상 | TEST4 |
| 봐야 할 지표 | `score_all_ms`, `evaluated_cell_check_ratio_pct`, `score_all_evaluated_candidates_total` |

TEST5는 kernel 자체를 더 빠르게 만든 실험이 아닙니다. GPU가 계산해야 할 candidate 수를 줄이는 실험입니다. 따라서 `score_all_ms`만 보면 안 되고, 실제로 얼마나 많은 계산이 제거됐는지를 같이 봐야 합니다.

이번 결과에서는 `evaluated_cell_check_ratio_pct`가 **99.97%**입니다. 즉, 전체 계산 중 약 **0.03%**만 제거되었습니다. 줄어든 계산량이 너무 적어서 CPU에서 pruning하는 비용이 더 크게 작용했고, TEST4보다 느려졌습니다.

### 보조 실험: TEST4 + pinned memory

| 항목 | 내용 |
| --- | --- |
| backend | `gpu_block_cached_map_shared_pinned` |
| 목적 | pinned host memory로 host-device transfer 비용을 줄일 수 있는지 확인 |
| 계산 방식 | TEST4 구조를 유지하고, 전송용 host buffer를 pinned memory로 관리 |
| 추가된 것 | reusable pinned host memory buffer |
| 비교 대상 | 같은 보조 실험 표 안의 TEST4 |
| 봐야 할 지표 | `score_all_ms`, `throughput`, `vs_TEST4_ms_change_pct` |

pinned memory는 CPU memory와 GPU memory 사이의 복사를 더 빠르게 만들기 위한 방법입니다. 하지만 pinned memory를 준비하고 관리하는 비용도 있습니다. 전송량이 충분히 크거나 반복 구조가 잘 맞으면 이득이 날 수 있지만, 항상 빨라지는 것은 아닙니다.

이번 보조 실험에서는 TEST4+pinned가 TEST4보다 **19.72% 느렸습니다**. 현재 입력 크기와 실행 조건에서는 pinned memory 이득보다 관리 비용이 더 컸다고 해석할 수 있습니다.

## 메인 실험 결과

| TEST | backend | match_ms | score_all_ms | throughput | CPU speedup | 이전 단계 대비 | 해석 |
| --- | --- | ---: | ---: | ---: | ---: | ---: | --- |
| TEST1 | `cpu_baseline` | 44.435 | 41.820 | 102.681 | 1.00x | 0.00% | 기준 성능 |
| TEST2 | `gpu_thread_per_candidate` | 17.525 | 14.366 | 298.944 | 2.91x | -65.65% | candidate 단위 GPU 병렬화만으로 큰 개선 |
| TEST3 | `gpu_block_per_candidate` | 15.369 | 11.865 | 362.142 | 3.52x | -17.41% | scan point 합산까지 block 내부 병렬화해서 추가 개선 |
| TEST4 | `gpu_block_cached_map_shared` | 10.317 | 4.839 | 888.067 | 8.64x | -59.22% | map cache + shared memory 효과가 가장 큼 |
| TEST5 | `gpu_block_cached_map_shared_pruned` | 11.733 | 7.543 | 569.716 | 5.54x | +55.88% | pruning 비용 때문에 TEST4보다 느림 |

단위:

- `match_ms`, `score_all_ms`: ms
- `throughput`: Mchecks/s
- `이전 단계 대비`: `score_all_ms` 변화율입니다. 음수면 빨라진 것입니다.

## 왜 TEST4가 가장 좋은가

`score_all()`은 candidate마다 scan point를 map grid에 대입해 점수를 계산합니다. 이때 TEST4는 다음 두 가지 비용을 줄였습니다.

1. map grid를 매번 GPU로 다시 복사하지 않고 GPU memory에 cache합니다.
2. candidate 내부 scan point 합산을 shared memory reduction으로 처리합니다.

그래서 TEST3에서 TEST4로 넘어갈 때 `score_all_ms`가 **11.865 ms -> 4.839 ms**로 줄었습니다. 같은 구간에서 throughput은 **362.142 -> 888.067 Mchecks/s**로 증가했습니다.

## TEST5 pruning이 느려진 이유

TEST5는 GPU로 보내기 전에 CPU에서 불필요한 candidate를 제거하는 실험입니다. 하지만 이번 결과에서는 실제 평가 check 비율이 **99.97%**입니다.

| TEST | input cell checks | evaluated cell checks | evaluated ratio |
| --- | ---: | ---: | ---: |
| TEST4 | 16,510,177,126 | 16,510,177,126 | 100.00% |
| TEST5 | 16,510,177,126 | 16,505,887,718 | 99.97% |

즉 전체 check 중 약 **0.03%만 제거**되었습니다. 줄어든 계산량이 너무 작아서 pruning을 하기 위한 CPU 검사 비용을 상쇄하지 못했습니다. 그래서 TEST5는 TEST4보다 `score_all_ms`가 **55.88% 증가**했습니다.

보고서에는 다음처럼 쓰면 됩니다.

> TEST5의 CPU-side pruning은 평가 대상 cell check를 99.97%까지밖에 줄이지 못했다. 제거된 계산량이 매우 작아 pruning overhead가 더 크게 작용했고, 결과적으로 TEST4 대비 성능이 저하되었다.

## Pinned Memory 보조 실험

pinned memory 비교는 아래 파일을 보면 됩니다.

```bash
cat results/block_ablation_pinned/block_ablation_table.md
```

이 실험은 TEST3, TEST4, TEST4+pinned만 따로 다시 실행한 보조 실험입니다. 따라서 위 메인 실험 표의 TEST4 절대값과 1:1로 비교하기보다는, 아래 표 안에서 TEST4와 TEST4+pinned의 상대 차이를 보는 것이 맞습니다.

그래프:

- [block_ablation_pinned/score_all_ms_avg.png](block_ablation_pinned/score_all_ms_avg.png)
- [block_ablation_pinned/throughput_avg.png](block_ablation_pinned/throughput_avg.png)
- [block_ablation_pinned/cpu_speedup.png](block_ablation_pinned/cpu_speedup.png)

| 실험 | backend | score_all_ms | throughput | TEST3 대비 | TEST4 대비 |
| --- | --- | ---: | ---: | ---: | ---: |
| TEST3 | `gpu_block_per_candidate` | 15.104 | 284.107 | 1.00x | +109.92% |
| TEST4 | `gpu_block_cached_map_shared` | 7.195 | 597.302 | 2.10x | 0.00% |
| TEST4 + pinned | `gpu_block_cached_map_shared_pinned` | 8.614 | 498.882 | 1.75x | +19.72% |

이 보조 실험에서는 pinned memory가 TEST4보다 느렸습니다. 현재 입력 크기와 실행 조건에서는 pinned host buffer 관리 비용이 전송 최적화 이득보다 큰 것으로 보면 됩니다.

## 최종 보고서용 정리

이번 실험에서는 CPU baseline 대비 GPU 병렬화를 단계적으로 적용했다. 단순 thread-per-candidate 방식(TEST2)은 CPU 대비 2.91x 개선되었고, block-per-candidate 방식(TEST3)은 3.52x까지 개선되었다. 가장 큰 개선은 map GPU cache와 shared memory reduction을 적용한 TEST4에서 나타났으며, `score_all_ms`는 41.820 ms에서 4.839 ms로 감소해 CPU 대비 8.64x speedup을 달성했다.

반면 TEST5의 candidate pruning은 실제 평가량을 99.97% 수준으로 거의 줄이지 못해 pruning overhead가 더 크게 작용했다. pinned memory 보조 실험 역시 TEST4 대비 19.72% 느려져, 현재 실험 조건에서는 TEST4가 최종 선택으로 가장 타당하다.

## 파일 구분

| 파일 | 의미 |
| --- | --- |
| [perf_runs.csv](perf_runs.csv) | 일부 또는 단일 실험 결과가 들어간 CSV |
| [perf_runs_all.csv](perf_runs_all.csv) | TEST1~TEST5 전체 비교용 raw CSV |
| [all_tests/perf_analysis_table.md](all_tests/perf_analysis_table.md) | 메인 실험 비교표 |
| [all_tests/perf_analysis_summary.csv](all_tests/perf_analysis_summary.csv) | 메인 실험 분석 CSV |
| [block_ablation_pinned/block_ablation_table.md](block_ablation_pinned/block_ablation_table.md) | TEST3, TEST4, TEST4+pinned 비교표 |

표 안의 `runs`는 같은 label로 몇 번 반복 실행했는지를 뜻합니다. 현재는 각 TEST를 한 번씩 실행했으므로 대부분 `runs=1`입니다.
