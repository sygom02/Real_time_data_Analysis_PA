# TEST2 기준 개별 모듈 적용 실험

이 문서는 기존 누적 결과를 재계산한 것이 아니라, **TEST2 구조에 각 모듈을 따로 붙인 backend를 새로 만들어 실제 실행한 결과**입니다.

자세한 로그와 그래프는 [isolated_modules/README.md](isolated_modules/README.md)에 정리되어 있습니다.

## 실험 기준

공통 기준은 TEST2입니다.

| 기준 | backend | 의미 |
| --- | --- | --- |
| TEST2 baseline | `gpu_thread_per_candidate` | CUDA thread 1개가 candidate 1개 처리 |

여기에 모듈을 하나씩 따로 적용했습니다.

| 실험 | backend | 의미 |
| --- | --- | --- |
| TEST2 + TEST3 | `gpu_block_per_candidate` | block 1개가 candidate 1개 처리 |
| TEST2 + TEST4 only | `gpu_thread_cached_map` | TEST2 thread 구조 유지 + map cache만 추가 |
| TEST2 + TEST5 only | `gpu_thread_pruned` | TEST2 thread 구조 유지 + pruning만 추가 |

## 결과 요약

| 비교 | score_all_ms | TEST2 대비 시간 변화 | TEST2 대비 speedup | throughput | TEST2 대비 throughput 변화 | 판단 |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| TEST2 baseline | 21.720 | 0.00% | 1.00x | 197.065 | 0.00% | 기준 |
| TEST2 + TEST3 | 16.682 | -23.20% | 1.30x | 257.083 | +30.46% | 개선됨 |
| TEST2 + TEST4 only | 11.963 | -44.92% | 1.82x | 359.061 | +82.20% | 가장 좋음 |
| TEST2 + TEST5 only | 20.346 | -6.33% | 1.07x | 210.609 | +6.87% | 거의 효과 없음 |

결론:

```text
개별 모듈 효과:
TEST4 map cache > TEST3 block-per-candidate > TEST5 pruning
```

## 해석

TEST2에 TEST3의 block-per-candidate 구조를 적용하면 `score_all_ms`가 **21.720 ms -> 16.682 ms**로 줄어 **1.30x** 빨라졌습니다. candidate 내부 scan point 합산을 여러 thread가 나눠 처리한 효과입니다.

TEST2에 TEST4의 map cache만 적용하면 `score_all_ms`가 **21.720 ms -> 11.963 ms**로 줄어 **1.82x** 빨라졌습니다. TEST2 구조를 유지했는데도 가장 큰 개선이 나왔으므로, map을 매번 GPU로 복사하던 비용이 꽤 컸다고 볼 수 있습니다.

TEST2에 TEST5의 pruning만 적용하면 `score_all_ms`가 **21.720 ms -> 20.346 ms**로 줄어 **1.07x** 정도만 빨라졌습니다. evaluated cell check ratio가 **99.97%**라서 실제 제거된 계산량이 거의 없었습니다.

## 최종 보고서용 문장

TEST2 `gpu_thread_per_candidate`를 기준으로 개별 모듈을 단독 적용해 비교했다. TEST3의 block-per-candidate 구조를 적용한 경우 `score_all_ms`는 21.720 ms에서 16.682 ms로 감소해 1.30x 성능 향상을 보였다. TEST4의 map GPU cache만 TEST2 구조에 적용한 경우 `score_all_ms`는 11.963 ms까지 감소해 1.82x speedup을 보였으며, 개별 모듈 중 가장 큰 개선 효과를 보였다. 반면 TEST5의 pruning만 적용한 경우 `score_all_ms`는 20.346 ms로 1.07x 개선에 그쳤고, 실제 evaluated cell check ratio가 99.97%로 거의 줄지 않아 pruning 효과는 제한적이었다.

따라서 TEST2 기준 개별 모듈 실험에서는 **map GPU cache가 가장 효과적인 단독 최적화 모듈**로 확인되었다.
