#pragma once

#include "skore_ecs_benchmark/version.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace skore_ecs_benchmark {

/**
 * Tunable parameters for the benchmark suite.
 * Fixed entity counts, warm-up passes and timed runs per scenario are
 * controlled here so results are reproducible.
 */
struct benchmark_config {
	std::uint32_t entity_count = 100000u;
	std::uint32_t warmup_runs = 2u;
	std::uint32_t timed_runs = 5u;
};

/**
 * Result of a single benchmark scenario: best / average / worst wall time
 * over @ref timed_runs and the derived throughput.
 */
struct benchmark_result {
	std::string name;
	std::uint64_t operations;
	double best_seconds = 0.0;
	double average_seconds = 0.0;
	double worst_seconds = 0.0;
	double operations_per_second = 0.0;
};

/**
 * Run the full Skore ECS benchmark suite.
 *
 * Bootstraps the Skore app context (which auto-loads the sk-entities plugin
 * from {app_folder}/plugins), obtains the sk_entities_api_t from the app
 * registry, then runs every scenario in @ref config's fixed-size worlds.
 * Results are collected in @p out_results (cleared on entry).
 *
 * @param config     Suite parameters (entity counts, warm-up, timed runs).
 * @param out_results Receives one benchmark_result per scenario.
 * @return 0 on success; non-zero if bootstrap or ECS lookup failed.
 */
int run_suite(const benchmark_config& config, std::vector<benchmark_result>& out_results);

/**
 * Print a formatted results report to stdout.
 * @param results Results produced by run_suite.
 */
void print_report(const std::vector<benchmark_result>& results);

} // namespace skore_ecs_benchmark
