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
	/** Number of query/system iterations per timed run for the iteration
	 *  scenarios (iterate_position, iterate_position_velocity, system_update). */
	std::uint32_t query_iterations = 100u;
};

/**
 * Result of a single benchmark scenario: best / average / worst wall time
 * over @ref benchmark_config::timed_runs and the derived throughput.
 */
struct benchmark_result {
	std::string name;
	std::uint64_t operations = 0u;
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
 * registry, then runs every scenario in @p config's fixed-size worlds.
 * Results are collected in @p out_results (cleared on entry).
 *
 * @param config       Suite parameters (entity counts, warm-up, timed runs).
 * @param out_results  Receives one benchmark_result per scenario.
 * @return 0 on success; non-zero if bootstrap or ECS lookup failed.
 */
int run_skore_suite(const benchmark_config& config, std::vector<benchmark_result>& out_results);

/**
 * Run the full Flecs benchmark suite with scenarios equivalent to the Skore
 * suite (same entity counts, component shapes and iteration counts), so the
 * two backends can be compared under one harness.
 *
 * @param config       Suite parameters (entity counts, warm-up, timed runs).
 * @param out_results  Receives one benchmark_result per scenario.
 * @return 0 on success; non-zero if Flecs initialization failed.
 */
int run_flecs_suite(const benchmark_config& config, std::vector<benchmark_result>& out_results);

/**
 * Print a formatted results report to stdout.
 * @param title   Report heading (e.g. "Skore ECS").
 * @param results Results produced by run_skore_suite / run_flecs_suite.
 */
void print_report(const char* title, const std::vector<benchmark_result>& results);

/**
 * Print a side-by-side comparison of the Skore and Flecs suites to stdout.
 * @param skore_results Skore suite results.
 * @param flecs_results Flecs suite results.
 */
void print_comparison(const std::vector<benchmark_result>& skore_results, const std::vector<benchmark_result>& flecs_results);

/**
 * Write the comparison results to a file (Markdown table).
 * @param path          Destination file path.
 * @param config        Config used to produce the results (recorded as metadata).
 * @param skore_results Skore suite results.
 * @param flecs_results Flecs suite results.
 * @return 0 on success, non-zero on I/O failure.
 */
int save_results(const char* path, const benchmark_config& config, const std::vector<benchmark_result>& skore_results, const std::vector<benchmark_result>& flecs_results);

} // namespace skore_ecs_benchmark
