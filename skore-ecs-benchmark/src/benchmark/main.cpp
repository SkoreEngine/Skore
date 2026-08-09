#include "skore_ecs_benchmark/benchmark.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {

void print_usage(const char* prog) {
	std::cout << "Usage: " << prog << " [options]\n"
			  << "\n"
			  << "Runs the Skore ECS and Flecs benchmark suites side by side and reports\n"
			  << "the comparison (console + results file).\n"
			  << "\n"
			  << "Options:\n"
			  << "  --entity-count N       entities per scenario (default 100000)\n"
			  << "  --query-iterations N   iterations for query/system scenarios (default 100)\n"
			  << "  --warmup-runs N        untimed warm-up passes (default 2)\n"
			  << "  --timed-runs N         timed passes per scenario (default 5)\n"
			  << "  --out PATH             comparison results file (default results/comparison.md)\n"
			  << "  --help                 show this help\n";
}

std::uint32_t parse_uint(const char* name, const char* value) {
	char* end = nullptr;
	const unsigned long v = std::strtoul(value, &end, 10);
	if (end == value || *end != '\0' || v == 0 || v > 0xFFFFFFFFUL) {
		std::cerr << "invalid value for " << name << ": " << value << "\n";
		std::exit(1);
	}
	return static_cast<std::uint32_t>(v);
}

} // namespace

int main(int argc, char** argv) {
	skore_ecs_benchmark::benchmark_config config;
	std::string out_path = "results/comparison.md";

	for (int i = 1; i < argc; ++i) {
		const std::string arg = argv[i];
		if (arg == "--help") {
			print_usage(argv[0]);
			return 0;
		}
		auto need_value = [&](const char* name) -> const char* {
			if (i + 1 >= argc) {
				std::cerr << "missing value for " << name << "\n";
				std::exit(1);
			}
			return argv[++i];
		};
		if (arg == "--entity-count") {
			config.entity_count = parse_uint("--entity-count", need_value("--entity-count"));
		} else if (arg == "--query-iterations") {
			config.query_iterations = parse_uint("--query-iterations", need_value("--query-iterations"));
		} else if (arg == "--warmup-runs") {
			config.warmup_runs = parse_uint("--warmup-runs", need_value("--warmup-runs"));
		} else if (arg == "--timed-runs") {
			config.timed_runs = parse_uint("--timed-runs", need_value("--timed-runs"));
		} else if (arg == "--out") {
			out_path = need_value("--out");
		} else {
			std::cerr << "unknown option: " << arg << "\n";
			print_usage(argv[0]);
			return 1;
		}
	}

	std::cout << "skore-ecs-benchmark v" << SKORE_ECS_BENCHMARK_VERSION << "\n";
	std::cout << "entities/scenario: " << config.entity_count << "  query iterations: " << config.query_iterations << "  warmup: " << config.warmup_runs
			  << "  timed: " << config.timed_runs << "\n";

	std::vector<skore_ecs_benchmark::benchmark_result> skore_results;
	std::vector<skore_ecs_benchmark::benchmark_result> flecs_results;

	std::cout << "\n[skore] running suite...\n";
	if (skore_ecs_benchmark::run_skore_suite(config, skore_results) != 0) {
		std::cerr << "skore suite failed\n";
		return 1;
	}

	std::cout << "[flecs] running suite...\n";
	if (skore_ecs_benchmark::run_flecs_suite(config, flecs_results) != 0) {
		std::cerr << "flecs suite failed\n";
		return 1;
	}

	skore_ecs_benchmark::print_report("Skore ECS", skore_results);
	skore_ecs_benchmark::print_report("Flecs 4.1.6", flecs_results);
	skore_ecs_benchmark::print_comparison(skore_results, flecs_results);

	if (skore_ecs_benchmark::save_results(out_path.c_str(), config, skore_results, flecs_results) != 0) {
		return 1;
	}
	std::cout << "\nResults written to " << out_path << "\n";
	return 0;
}
