# skore-ecs-benchmark comparison results

- entities per scenario: 100000
- warm-up runs: 2
- timed runs: 5
- query/system iterations per timed run: 100
- checksum: 18446742206248677075

| scenario | skore_avg(s) | flecs_avg(s) | skore/flecs | skore_ops/s | flecs_ops/s |
| --- | ---: | ---: | ---: | ---: | ---: |
| create_entities | 0.170951 | 0.00867044 | 19.7165x | 584963 | 1.15334e+07 |
| destroy_entities | 0.0166676 | 0.00188771 | 8.8295x | 5.99967e+06 | 5.29741e+07 |
| add_component | 0.178623 | 0.00775973 | 23.0193x | 559837 | 1.2887e+07 |
| remove_component | 0.110694 | 0.00635342 | 17.4227x | 903393 | 1.57396e+07 |
| iterate_position | 0.0083626 | 0.00653246 | 1.28016x | 1.1958e+09 | 1.53082e+09 |
| iterate_position_velocity | 0.00747825 | 0.00957459 | 0.781052x | 1.33721e+09 | 1.04443e+09 |
| system_update | 0.00787657 | 0.00890247 | 0.884762x | 1.26959e+09 | 1.12328e+09 |
| deferred_spawn | 0.162397 | 0.0144773 | 11.2174x | 615774 | 6.90737e+06 |
