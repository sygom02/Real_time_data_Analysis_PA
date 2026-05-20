# 최종 실험 결과 통합 정리

이 문서는 지금까지 수행한 세 가지 실험을 한 번에 보기 좋게 정리한 것입니다.

| 구분 | 목적 | 결과 위치 |
| --- | --- | --- |
| 누적 적용 실험 | TEST1부터 TEST5까지 최적화를 단계적으로 누적 적용 | [all_tests/](all_tests/) |
| 개별 모듈 실험 | TEST2를 기준으로 TEST3/TEST4/TEST5 모듈을 각각 따로 적용 | [isolated_modules/](isolated_modules/) |
| pinned memory 보조 실험 | TEST4에 pinned memory를 붙였을 때 효과 확인 | [block_ablation_pinned/](block_ablation_pinned/) |

## 전체 결론

가장 효과적인 최적화는 **map GPU cache(TEST4)** 입니다.

- 누적 실험에서도 TEST4가 가장 빠름: **4.839 ms**
- 개별 모듈 실험에서도 TEST4 cache-only가 가장 큰 개선: **TEST2 대비 1.82x**
- pruning(TEST5)은 제거되는 계산량이 너무 적어 효과가 작거나 오히려 손해
- pinned memory는 현재 조건에서는 TEST4보다 느려짐

최종 선택:

```text
gpu_block_cached_map_shared
```

즉 **TEST3 block-per-candidate 구조 + TEST4 map GPU cache** 조합이 가장 좋습니다.

## 1. 누적 적용 실험

이 실험은 최적화를 단계적으로 누적 적용한 결과입니다.

```text
TEST1 CPU baseline
-> TEST2 GPU thread-per-candidate
-> TEST3 block-per-candidate
-> TEST4 map cache + shared memory
-> TEST5 pruning
```

| TEST | backend | score_all_ms | throughput | CPU 대비 speedup | 이전 단계 대비 | 해석 |
| --- | --- | ---: | ---: | ---: | ---: | --- |
| TEST1 | `cpu_baseline` | 41.820 | 102.681 | 1.00x | 0.00% | CPU 기준 |
| TEST2 | `gpu_thread_per_candidate` | 14.366 | 298.944 | 2.91x | -65.65% | GPU 병렬화로 크게 개선 |
| TEST3 | `gpu_block_per_candidate` | 11.865 | 362.142 | 3.52x | -17.41% | block 구조로 추가 개선 |
| TEST4 | `gpu_block_cached_map_shared` | 4.839 | 888.067 | 8.64x | -59.22% | 가장 큰 개선 |
| TEST5 | `gpu_block_cached_map_shared_pruned` | 7.543 | 569.716 | 5.54x | +55.88% | pruning overhead로 TEST4보다 느림 |

해석:

TEST1에서 TEST2로 넘어가면서 CPU loop를 GPU로 옮긴 효과가 크게 나타났습니다. 이후 TEST3에서 candidate 내부 scan point 합산까지 병렬화하며 추가 개선이 있었고, TEST4에서 map을 GPU memory에 cache하면서 가장 큰 성능 향상이 나왔습니다.

TEST5는 TEST4에 pruning을 추가했지만 `score_all_ms`가 **4.839 ms -> 7.543 ms**로 증가했습니다. 실제 evaluated cell check ratio가 **99.97%**라 제거된 계산량이 거의 없었고, CPU pruning 비용이 더 크게 작용했습니다.

결론:

```text
누적 적용 기준 최적 결과 = TEST4
```

## 2. 개별 모듈 실험

이 실험은 TEST2를 baseline으로 두고, 각 모듈을 따로 붙여 실제로 다시 실행한 결과입니다.

| 비교 | backend | score_all_ms | TEST2 대비 시간 변화 | TEST2 대비 speedup | throughput | 해석 |
| --- | --- | ---: | ---: | ---: | ---: | --- |
| TEST2 baseline | `gpu_thread_per_candidate` | 21.720 | 0.00% | 1.00x | 197.065 | 기준 |
| TEST2 + TEST3 | `gpu_block_per_candidate` | 16.682 | -23.20% | 1.30x | 257.083 | block 구조 단독 효과 있음 |
| TEST2 + TEST4 only | `gpu_thread_cached_map` | 11.963 | -44.92% | 1.82x | 359.061 | map cache 단독 효과가 가장 큼 |
| TEST2 + TEST5 only | `gpu_thread_pruned` | 20.346 | -6.33% | 1.07x | 210.609 | pruning 단독 효과는 매우 작음 |

해석:

TEST2에 TEST3의 block-per-candidate 구조를 붙이면 `score_all_ms`가 **23.20% 감소**했습니다. candidate 하나를 thread 하나가 처리하는 것보다 block 하나가 처리하면서 scan point 합산을 나눠 하는 것이 더 좋았습니다.

TEST2에 TEST4의 map cache만 붙이면 `score_all_ms`가 **44.92% 감소**했습니다. TEST2의 thread-per-candidate 구조를 그대로 유지했는데도 가장 큰 개선이 나왔기 때문에, map을 매번 GPU로 복사하던 비용이 주요 병목이었다고 볼 수 있습니다.

TEST2에 TEST5 pruning만 붙이면 `score_all_ms`가 **6.33% 감소**에 그쳤습니다. pruning으로 실제 제거된 계산량이 약 **0.03%** 수준이라 단독 효과가 거의 없었습니다.

결론:

```text
개별 모듈 효과 = TEST4 map cache > TEST3 block 구조 > TEST5 pruning
```

## 3. Pinned Memory 보조 실험

이 실험은 TEST3, TEST4, TEST4+pinned를 따로 비교한 보조 실험입니다.

| 실험 | backend | score_all_ms | throughput | TEST3 대비 speedup | TEST4 대비 변화 | 해석 |
| --- | --- | ---: | ---: | ---: | ---: | --- |
| TEST3 | `gpu_block_per_candidate` | 15.104 | 284.107 | 1.00x | +109.92% | 기준 |
| TEST4 | `gpu_block_cached_map_shared` | 7.195 | 597.302 | 2.10x | 0.00% | map cache 효과 큼 |
| TEST4 + pinned | `gpu_block_cached_map_shared_pinned` | 8.614 | 498.882 | 1.75x | +19.72% | TEST4보다 느림 |

해석:

TEST4는 TEST3 대비 `score_all_ms`를 **52.36% 감소**시켰습니다. 이 보조 실험에서도 map cache는 확실히 효과가 있었습니다.

하지만 pinned memory를 추가한 TEST4+pinned는 TEST4보다 `score_all_ms`가 **19.72% 증가**했습니다. 현재 입력 크기와 실행 조건에서는 pinned buffer 관리 비용이 host-device transfer 최적화 이득보다 컸다고 해석할 수 있습니다.

결론:

```text
pinned memory는 현재 조건에서는 이득 없음
```

## 최종 비교 요약

| 관점 | 가장 좋은 결과 | 이유 |
| --- | --- | --- |
| 누적 적용 | TEST4 `gpu_block_cached_map_shared` | 전체 실험 중 `score_all_ms`가 가장 낮음 |
| 개별 모듈 | TEST4 cache-only `gpu_thread_cached_map` | TEST2 대비 단독 개선 폭이 가장 큼 |
| pinned 보조 | TEST4 without pinned | pinned 추가 시 오히려 느려짐 |
| pruning | 사용하지 않는 편이 좋음 | 제거되는 candidate/check가 너무 적음 |

최종적으로 보고서에는 아래처럼 정리하면 됩니다.

> 전체 누적 실험과 개별 모듈 실험을 종합하면, 가장 큰 성능 개선은 map grid를 GPU memory에 cache한 TEST4에서 나타났다. TEST4는 누적 실험에서 CPU baseline 대비 8.64x speedup을 보였고, 개별 모듈 실험에서도 TEST2 대비 1.82x 개선되어 단독 최적화 효과가 가장 컸다. 반면 pruning은 evaluated cell check ratio가 99.97%로 계산량을 거의 줄이지 못해 효과가 제한적이었고, pinned memory 역시 TEST4 대비 19.72% 느려져 현재 조건에서는 이득이 없었다. 따라서 최종적으로는 `gpu_block_cached_map_shared`가 가장 적절한 backend이다.

## 참고 파일

| 파일 | 내용 |
| --- | --- |
| [README.md](README.md) | 전체 결과 설명 |
| [module_improvement_README.md](module_improvement_README.md) | TEST2 기준 개별 모듈 실험 요약 |
| [all_tests/perf_analysis_table.md](all_tests/perf_analysis_table.md) | 누적 적용 실험 표 |
| [isolated_modules/README.md](isolated_modules/README.md) | 개별 모듈 실험 상세 |
| [block_ablation_pinned/block_ablation_table.md](block_ablation_pinned/block_ablation_table.md) | pinned memory 보조 실험 표 |
