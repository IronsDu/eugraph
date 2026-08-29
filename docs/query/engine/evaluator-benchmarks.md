# Expression Evaluator Benchmarks

> 当前版本：columnar_kernels + typed scalar functions。

| Benchmark | Baseline ns/op | Current ns/op | speedup |
|-----------|---------------|--------------|---------|
| bmEvaluatorInt64Add/1024 | 0.4 | 0.0 | 11.63x |
| bmEvaluatorInt64Add/4096 | 5.6 | 0.5 | 11.65x |
| bmEvaluatorInt64Add/32768 | 391.7 | 30.9 | 12.67x |
| bmEvaluatorInt64Add/262144 | 30286.4 | 2051.9 | 14.76x |
| bmEvaluatorInt64Add/1048576 | 440419.9 | 49065.3 | 8.98x |
| bmEvaluatorInt64Greater/1024 | 0.4 | 0.0 | 9.89x |
| bmEvaluatorInt64Greater/4096 | 5.8 | 0.6 | 10.37x |
| bmEvaluatorInt64Greater/32768 | 399.9 | 35.7 | 11.21x |
| bmEvaluatorInt64Greater/262144 | 27749.0 | 2259.9 | 12.28x |
| bmEvaluatorInt64Greater/1048576 | 440430.2 | 38572.7 | 11.42x |
| bmEvaluatorDoubleAdd/1024 | 0.5 | 0.0 | 12.52x |
| bmEvaluatorDoubleAdd/4096 | 7.4 | 0.6 | 12.29x |
| bmEvaluatorDoubleAdd/32768 | 487.4 | 39.5 | 12.35x |
| bmEvaluatorDoubleAdd/262144 | 50654.9 | 2542.1 | 19.93x |
| bmEvaluatorDoubleAdd/1048576 | 500633.8 | 54761.6 | 9.14x |
| bmEvaluatorInt64Negate/1024 | 0.3 | 0.0 | 8.73x |
| bmEvaluatorInt64Negate/4096 | 3.9 | 0.5 | 8.03x |
| bmEvaluatorInt64Negate/32768 | 282.0 | 31.2 | 9.02x |
| bmEvaluatorInt64Negate/262144 | 18470.8 | 2013.1 | 9.18x |
| bmEvaluatorInt64Negate/1048576 | 342585.0 | 44278.6 | 7.74x |
| bmEvaluatorInt64AddIndirect/1024 | - | 0.0 | 新增 indirect 基准 |
| bmEvaluatorInt64AddIndirect/4096 | - | 0.2 | 新增 indirect 基准 |
| bmEvaluatorInt64AddIndirect/32768 | - | 13.0 | 新增 indirect 基准 |
| bmEvaluatorInt64AddIndirect/262144 | - | 818.4 | 新增 indirect 基准 |
| bmEvaluatorInt64AddIndirect/1048576 | - | 16877.7 | 新增 indirect 基准 |

- CPU scaling / ASLR 开启，同一机器前后对比。

