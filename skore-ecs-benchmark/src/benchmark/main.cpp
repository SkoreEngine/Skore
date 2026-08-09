#include "skore_ecs_benchmark/benchmark.hpp"

#include <iostream>

int main(int argc, char** argv)
{
	std::cout << "skore-ecs-benchmark v" << SKORE_ECS_BENCHMARK_VERSION << "\n";
	std::cout << skore_ecs_benchmark::benchmark_hello() << "\n";
	return 0;
}
