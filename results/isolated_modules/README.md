# TEST2 기준 개별 모듈 실험 결과

이 폴더는 TEST2 `gpu_thread_per_candidate`를 baseline으로 두고, 각 최적화 모듈을 **개별적으로** 붙여 실제 실행한 결과입니다.

## 실험한 backend

| label | backend | 설명 |
| --- | --- | --- |
| `test2_baseline` | `gpu_thread_per_candidate` | TEST2 기준 |
| `test2_plus_test3_block` | `gpu_block_per_candidate` | TEST2 대비 block-per-candidate 구조 적용 |
| `test2_plus_test4_cached_map` | `gpu_thread_cached_map` | TEST2 thread 구조 유지 + map cache만 적용 |
| `test2_plus_test5_pruned` | `gpu_thread_pruned` | TEST2 thread 구조 유지 + pruning만 적용 |

새로 추가한 backend:

- `gpu_thread_cached_map`: TEST2 kernel 구조는 유지하고 map grid cache만 추가
- `gpu_thread_pruned`: TEST2 kernel 구조는 유지하고 CPU-side pruning만 추가

## 결과 표

| 비교 | samples | score_all_ms | TEST2 대비 시간 변화 | TEST2 대비 speedup | throughput | TEST2 대비 throughput 변화 | evaluated ratio |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| TEST2 baseline | 3341 | 21.720 | 0.00% | 1.00x | 197.065 | 0.00% | 100.00% |
| TEST2 + TEST3 block | 3661 | 16.682 | -23.20% | 1.30x | 257.083 | +30.46% | 100.00% |
| TEST2 + TEST4 cache-only | 3799 | 11.963 | -44.92% | 1.82x | 359.061 | +82.20% | 100.00% |
| TEST2 + TEST5 prune-only | 3481 | 20.346 | -6.33% | 1.07x | 210.609 | +6.87% | 99.97% |

단위:

- `score_all_ms`: ms
- `throughput`: Mchecks/s
- `TEST2 대비 시간 변화`: 음수면 TEST2보다 빨라진 것

## 결론

개별 모듈만 따로 붙였을 때 성능 개선 순서는 아래와 같습니다.

```text
TEST4 map cache only
> TEST3 block-per-candidate
> TEST5 pruning only
```

가장 효과적인 단독 모듈은 **map cache**입니다. TEST2 구조를 그대로 유지했는데도 `score_all_ms`가 **44.92% 감소**했습니다.

pruning-only는 거의 효과가 없었습니다. 실제 evaluated cell check ratio가 **99.97%**라서 제거된 계산량이 약 **0.03%**뿐이었습니다.

## 원본 파일

| 파일 | 의미 |
| --- | --- |
| [perf_runs.csv](perf_runs.csv) | 이번 개별 모듈 실험 raw CSV |
| [perf_analysis_summary.csv](perf_analysis_summary.csv) | 분석 요약 CSV |
| [perf_analysis_table.md](perf_analysis_table.md) | 자동 생성 요약표 |
| [status.tsv](status.tsv) | 각 run 성공 여부와 sample 수 |
| [score_all_ms_avg.png](score_all_ms_avg.png) | score_all 시간 그래프 |
| [throughput_avg.png](throughput_avg.png) | throughput 그래프 |
| [cpu_speedup.png](cpu_speedup.png) | TEST2 대비 speedup 그래프 |

실행 로그:

- [test2_baseline.log](test2_baseline.log)
- [test2_plus_test3_block.log](test2_plus_test3_block.log)
- [test2_plus_test4_cached_map.log](test2_plus_test4_cached_map.log)
- [test2_plus_test5_pruned.log](test2_plus_test5_pruned.log)

## 주의

각 실험은 `runs=1`입니다. 더 엄밀한 보고서 수치를 만들려면 같은 backend를 여러 번 반복 실행한 뒤 평균과 분산을 같이 보는 것이 좋습니다.

또한 backend 속도에 따라 처리한 scan sample 수가 조금 다릅니다. 그래서 최종 결론은 `score_all_ms` 평균과 throughput을 중심으로 해석했습니다.
