#include "skore_ecs_benchmark/benchmark.hpp"

#include "app.h"
#include "entities.h"
#include "flecs.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace skore_ecs_benchmark {

/* ------------------------------------------------------------------ */
/*  Shared scenario vocabulary                                         */
/* ------------------------------------------------------------------ */

/*
 * Component shapes used by every scenario, in both backends. Plain POD so the
 * two ECSs lay them out identically (Position 8 B, Velocity 8 B, Health 4 B).
 */
struct position_t {
	float x, y;
};
struct velocity_t {
	float x, y;
};
struct health_t {
	std::int32_t value;
};

/*
 * Skore component identities. Skore identifiers are 128-bit sk_type_id_t
 * values; the benchmark uses fixed, distinct constants (compile-time brace
 * init — SK_TYPE_ID's C compound literal is not valid C++).
 */
static const sk_type_id_t kPositionId = {0x0000000000000010ULL, 0x1000000000000000ULL};
static const sk_type_id_t kVelocityId = {0x0000000000000020ULL, 0x2000000000000000ULL};
static const sk_type_id_t kHealthId = {0x0000000000000030ULL, 0x3000000000000000ULL};

/* Identical to SK_ENTITIES_API_TYPE_ID, spelled out for C++ (see entities.h). */
static const sk_type_id_t kEntitiesApiId = {0x83bc3af6038a15d9ULL, 0x66d789c0523d7ddbULL};

/* Same set of scenarios for both backends, in the same order so the results
 * align when compared side by side. */
static const char* kScenarioNames[] = {
	"create_entities", "destroy_entities", "add_component", "remove_component", "iterate_position", "iterate_position_velocity", "system_update", "deferred_spawn",
};
static constexpr std::size_t kScenarioCount = sizeof(kScenarioNames) / sizeof(kScenarioNames[0]);

/* Non-optimizable sink so update work in iteration scenarios cannot be elided
 * by the optimizer; printed at the end of each suite. */
static volatile std::uint64_t g_checksum = 0u;

/* ------------------------------------------------------------------ */
/*  Timing helper                                                      */
/* ------------------------------------------------------------------ */

template <typename F> static double time_once(F&& fn) {
	const auto t0 = std::chrono::steady_clock::now();
	fn();
	const auto t1 = std::chrono::steady_clock::now();
	return std::chrono::duration<double>(t1 - t0).count();
}

template <typename F> static void run_measure(benchmark_result& res, const benchmark_config& config, F&& fn) {
	for (std::uint32_t i = 0u; i < config.warmup_runs; ++i) {
		fn();
	}
	double best = 1e30;
	double worst = 0.0;
	double sum = 0.0;
	for (std::uint32_t i = 0u; i < config.timed_runs; ++i) {
		const double seconds = time_once(fn);
		best = std::min(best, seconds);
		worst = std::max(worst, seconds);
		sum += seconds;
	}
	res.best_seconds = best;
	res.average_seconds = sum / static_cast<double>(config.timed_runs);
	res.worst_seconds = worst;
	res.operations_per_second = res.average_seconds > 0.0 ? static_cast<double>(res.operations) / res.average_seconds : 0.0;
}

/* ------------------------------------------------------------------ */
/*  Skore ECS backend                                                  */
/* ------------------------------------------------------------------ */

namespace {

struct skore_backend_t {
	sk_app_context_t* context = nullptr;
	const sk_entities_api_t* ecs = nullptr;

	bool init() {
		context = sk_app_init(0, nullptr);
		if (context == nullptr) {
			std::cerr << "skore: sk_app_init failed\n";
			return false;
		}
		const sk_app_api_t* app_api = sk_app_api();
		ecs = static_cast<const sk_entities_api_t*>(app_api->get_api(context, kEntitiesApiId));
		if (ecs == nullptr) {
			std::cerr << "skore: sk_entities_api_t not registered (plugin not loaded?)\n";
			sk_app_destroy(context);
			context = nullptr;
			return false;
		}
		ecs->register_component(kPositionId, sizeof(position_t), alignof(position_t), "Position");
		ecs->register_component(kVelocityId, sizeof(velocity_t), alignof(velocity_t), "Velocity");
		ecs->register_component(kHealthId, sizeof(health_t), alignof(health_t), "Health");
		return true;
	}

	void shutdown() {
		if (context != nullptr) {
			sk_app_destroy(context);
			context = nullptr;
			ecs = nullptr;
		}
	}
};

/* Spawn @p count entities in @p world, each holding the given signature.
 * When @p out is non-null, the created handles are appended to it. */
static std::uint32_t spawn_with(const skore_backend_t& sb, sk_world_t* world, const sk_type_id_t* ids, std::uint32_t id_count, std::uint32_t count, std::vector<sk_entity_t>* out) {
	std::uint32_t spawned = 0u;
	for (std::uint32_t i = 0u; i < count; ++i) {
		const sk_entity_t e = sb.ecs->world_spawn(world, ids, id_count);
		if (e.index != 0u) {
			if (out != nullptr) {
				out->push_back(e);
			}
			++spawned;
		}
	}
	return spawned;
}

/* Context handed to the move system: the ECS table and the world-managed query. */
struct skore_move_ctx_t {
	const sk_entities_api_t* ecs;
	sk_query_t* query;
};

static void skore_system_update_cb(sk_world_t* world, f32 delta_time, void_ptr_t user_data) {
	(void)delta_time;
	(void)world;
	const skore_move_ctx_t* ctx = static_cast<const skore_move_ctx_t*>(user_data);
	SK_ECS_QUERY_FOREACH(ctx->ecs, ctx->query, it) {
		SK_ECS_ROW_FOREACH(it) {
			position_t* p = SK_ECS_ITER_AT(it, 1, position_t);
			velocity_t* v = SK_ECS_ITER_AT(it, 2, velocity_t);
			p->x += (v != nullptr) ? v->x : 1.0f;
			g_checksum += static_cast<std::uint64_t>(p->x);
		}
	}
}

} // namespace

static std::vector<benchmark_result> run_skore_suite_impl(const benchmark_config& config) {
	std::vector<benchmark_result> results;
	results.reserve(kScenarioCount);

	skore_backend_t sb;
	if (!sb.init()) {
		return results;
	}

	const sk_entities_api_t* ecs = sb.ecs;
	const std::uint32_t n = config.entity_count;

	const sk_type_id_t pos_sig[1] = {kPositionId};
	const sk_type_id_t pos_vel_sig[2] = {kPositionId, kVelocityId};

	for (std::size_t s = 0; s < kScenarioCount; ++s) {
		benchmark_result res;
		res.name = kScenarioNames[s];

		switch (s) {
		case 0: { /* create_entities: spawn n Position+Velocity entities */
			res.operations = n;
			sk_world_t* world = ecs->world_create();
			run_measure(res, config, [&]() {
				for (std::uint32_t i = 0u; i < n; ++i) {
					ecs->world_spawn(world, pos_vel_sig, 2u);
				}
			});
			ecs->world_destroy(world);
			break;
		}
		case 1: { /* destroy_entities: spawn n, then despawn them */
			res.operations = n;
			sk_world_t* world = ecs->world_create();
			std::vector<sk_entity_t> entities;
			entities.reserve(n);
			run_measure(res, config, [&]() {
				spawn_with(sb, world, pos_vel_sig, 2u, n, &entities);
				for (const sk_entity_t e : entities) {
					ecs->world_despawn(world, e);
				}
				entities.clear();
			});
			ecs->world_destroy(world);
			break;
		}
		case 2: { /* add_component: spawn with Position, add Velocity */
			res.operations = n;
			sk_world_t* world = ecs->world_create();
			std::vector<sk_entity_t> entities;
			entities.reserve(n);
			run_measure(res, config, [&]() {
				spawn_with(sb, world, pos_sig, 1u, n, &entities);
				for (const sk_entity_t e : entities) {
					ecs->world_add_component(world, e, kVelocityId);
				}
				entities.clear();
			});
			ecs->world_destroy(world);
			break;
		}
		case 3: { /* remove_component: spawn with Position+Velocity, remove Velocity */
			res.operations = n;
			sk_world_t* world = ecs->world_create();
			std::vector<sk_entity_t> entities;
			entities.reserve(n);
			run_measure(res, config, [&]() {
				spawn_with(sb, world, pos_vel_sig, 2u, n, &entities);
				for (const sk_entity_t e : entities) {
					ecs->world_remove_component(world, e, kVelocityId);
				}
				entities.clear();
			});
			ecs->world_destroy(world);
			break;
		}
		case 4: { /* iterate_position: query Position, update x */
			res.operations = static_cast<std::uint64_t>(n) * config.query_iterations;
			sk_world_t* world = ecs->world_create();
			spawn_with(sb, world, pos_vel_sig, 2u, n, nullptr);
			const sk_query_desc_t desc = {pos_sig, 1u, nullptr, 0u, nullptr, 0u};
			sk_query_t* query = ecs->world_query_create(world, &desc);
			run_measure(res, config, [&]() {
				for (std::uint32_t it = 0u; it < config.query_iterations; ++it) {
					SK_ECS_QUERY_FOREACH(ecs, query, var) {
						SK_ECS_ROW_FOREACH(var) {
							position_t* p = SK_ECS_ITER_AT(var, 1, position_t);
							p->x += 1.0f;
							g_checksum += static_cast<std::uint64_t>(p->x);
						}
					}
				}
			});
			ecs->world_destroy(world);
			break;
		}
		case 5: { /* iterate_position_velocity: query Position+Velocity, p += v */
			res.operations = static_cast<std::uint64_t>(n) * config.query_iterations;
			sk_world_t* world = ecs->world_create();
			spawn_with(sb, world, pos_vel_sig, 2u, n, nullptr);
			const sk_query_desc_t desc = {pos_vel_sig, 2u, nullptr, 0u, nullptr, 0u};
			sk_query_t* query = ecs->world_query_create(world, &desc);
			run_measure(res, config, [&]() {
				for (std::uint32_t it = 0u; it < config.query_iterations; ++it) {
					SK_ECS_QUERY_FOREACH(ecs, query, var) {
						SK_ECS_ROW_FOREACH(var) {
							position_t* p = SK_ECS_ITER_AT(var, 1, position_t);
							velocity_t* v = SK_ECS_ITER_AT(var, 2, velocity_t);
							p->x += v->x;
							g_checksum += static_cast<std::uint64_t>(p->x);
						}
					}
				}
			});
			ecs->world_destroy(world);
			break;
		}
		case 6: { /* system_update: run a registered system (dependency-graph API) */
			res.operations = static_cast<std::uint64_t>(n) * config.query_iterations;
			sk_world_t* world = ecs->world_create();
			spawn_with(sb, world, pos_vel_sig, 2u, n, nullptr);
			const sk_query_desc_t desc = {pos_vel_sig, 2u, nullptr, 0u, nullptr, 0u};
			skore_move_ctx_t ctx = {ecs, ecs->world_query_create(world, &desc)};
			const sk_type_id_t reads[1] = {kVelocityId};
			const sk_type_id_t writes[1] = {kPositionId};
			const sk_system_desc_t sdesc = {
				.callback = skore_system_update_cb,
				.reads = reads,
				.read_count = 1u,
				.writes = writes,
				.write_count = 1u,
				.name = "move",
				.user_data = &ctx,
			};
			sk_system_t* system = ecs->system_create(&sdesc);
			run_measure(res, config, [&]() {
				for (std::uint32_t it = 0u; it < config.query_iterations; ++it) {
					ecs->system_run(system, world, 0.016f);
				}
			});
			ecs->system_destroy(system);
			ecs->world_destroy(world);
			break;
		}
		case 7: { /* deferred_spawn: record n spawns, apply once */
			res.operations = n;
			sk_world_t* world = ecs->world_create();
			sk_entitycommands_t* commands = ecs->commands_create();
			run_measure(res, config, [&]() {
				for (std::uint32_t i = 0u; i < n; ++i) {
					ecs->commands_spawn(commands, pos_vel_sig, 2u);
				}
				ecs->commands_apply(commands, world);
			});
			ecs->commands_destroy(commands);
			ecs->world_destroy(world);
			break;
		}
		default:
			break;
		}

		results.push_back(std::move(res));
	}

	sb.shutdown();
	return results;
}

/* ------------------------------------------------------------------ */
/*  Flecs backend                                                      */
/* ------------------------------------------------------------------ */

namespace {

struct flecs_backend_t {
	ecs_world_t* world = nullptr;
	ecs_entity_t position = 0;
	ecs_entity_t velocity = 0;
	ecs_entity_t health = 0;
	ecs_id_t pos_vel_ids[3] = {0, 0, 0};

	/* Per-scenario world: init + register components so scenarios are
	 * independent (mirrors the Skore backend, which uses one world each). */
	bool init_world() {
		world = ecs_init();
		if (world == nullptr) {
			std::cerr << "flecs: ecs_init failed\n";
			return false;
		}
		position = make_component(world, "Position", sizeof(position_t), alignof(position_t));
		velocity = make_component(world, "Velocity", sizeof(velocity_t), alignof(velocity_t));
		health = make_component(world, "Health", sizeof(health_t), alignof(health_t));
		pos_vel_ids[0] = position;
		pos_vel_ids[1] = velocity;
		pos_vel_ids[2] = 0;
		return position != 0 && velocity != 0;
	}

	void fini_world() {
		if (world != nullptr) {
			ecs_fini(world);
			world = nullptr;
			position = 0;
			velocity = 0;
			health = 0;
		}
	}

private:
	static ecs_entity_t make_component(ecs_world_t* w, const char* name, std::size_t size, std::size_t align) {
		ecs_entity_desc_t ed = {};
		ed.name = name;
		ed.use_low_id = true;
		const ecs_entity_t entity = ecs_entity_init(w, &ed);

		ecs_component_desc_t cd = {};
		cd.entity = entity;
		cd.type.size = static_cast<ecs_size_t>(size);
		cd.type.alignment = static_cast<ecs_size_t>(align);
		return ecs_component_init(w, &cd);
	}
};

static void flecs_system_update_cb(ecs_iter_t* it) {
	position_t* p = ecs_field(it, position_t, 0);
	velocity_t* v = ecs_field(it, velocity_t, 1);
	for (std::int32_t i = 0; i < it->count; ++i) {
		p[i].x += v[i].x;
		g_checksum += static_cast<std::uint64_t>(p[i].x);
	}
}

/* Spawn @p count Position+Velocity entities via the bulk API; when @p out is
 * non-null, the created handles are copied into it. */
static std::uint32_t flecs_bulk_spawn(const flecs_backend_t& fb, std::uint32_t count, std::vector<ecs_entity_t>* out) {
	ecs_bulk_desc_t bd = {};
	bd.count = static_cast<std::int32_t>(count);
	bd.ids[0] = fb.position;
	bd.ids[1] = fb.velocity;
	const ecs_entity_t* created = ecs_bulk_init(fb.world, &bd);
	if (created != nullptr && out != nullptr) {
		out->assign(created, created + count);
	}
	return count;
}

} // namespace

static std::vector<benchmark_result> run_flecs_suite_impl(const benchmark_config& config) {
	std::vector<benchmark_result> results;
	results.reserve(kScenarioCount);

	flecs_backend_t fb;
	const std::uint32_t n = config.entity_count;

	for (std::size_t s = 0; s < kScenarioCount; ++s) {
		benchmark_result res;
		res.name = kScenarioNames[s];

		if (!fb.init_world()) {
			break;
		}
		ecs_world_t* world = fb.world;

		switch (s) {
		case 0: { /* create_entities: spawn n Position+Velocity entities */
			res.operations = n;
			run_measure(res, config, [&]() {
				for (std::uint32_t i = 0u; i < n; ++i) {
					ecs_entity_desc_t ed = {};
					ed.add = fb.pos_vel_ids;
					ecs_entity_init(world, &ed);
				}
			});
			break;
		}
		case 1: { /* destroy_entities: spawn n, then delete them */
			res.operations = n;
			std::vector<ecs_entity_t> entities;
			entities.reserve(n);
			run_measure(res, config, [&]() {
				flecs_bulk_spawn(fb, n, &entities);
				for (const ecs_entity_t e : entities) {
					ecs_delete(world, e);
				}
				entities.clear();
			});
			break;
		}
		case 2: { /* add_component: spawn with Position, add Velocity */
			res.operations = n;
			std::vector<ecs_entity_t> entities;
			entities.reserve(n);
			run_measure(res, config, [&]() {
				ecs_bulk_desc_t bd = {};
				bd.count = static_cast<std::int32_t>(n);
				bd.ids[0] = fb.position;
				const ecs_entity_t* created = ecs_bulk_init(world, &bd);
				entities.assign(created, created + n);
				for (const ecs_entity_t e : entities) {
					ecs_add_id(world, e, fb.velocity);
				}
				entities.clear();
			});
			break;
		}
		case 3: { /* remove_component: spawn with Position+Velocity, remove Velocity */
			res.operations = n;
			std::vector<ecs_entity_t> entities;
			entities.reserve(n);
			run_measure(res, config, [&]() {
				flecs_bulk_spawn(fb, n, &entities);
				for (const ecs_entity_t e : entities) {
					ecs_remove_id(world, e, fb.velocity);
				}
				entities.clear();
			});
			break;
		}
		case 4: { /* iterate_position: query Position, update x */
			res.operations = static_cast<std::uint64_t>(n) * config.query_iterations;
			flecs_bulk_spawn(fb, n, nullptr);
			ecs_query_desc_t qd = {};
			qd.terms[0].id = fb.position;
			ecs_query_t* query = ecs_query_init(world, &qd);
			run_measure(res, config, [&]() {
				for (std::uint32_t it = 0u; it < config.query_iterations; ++it) {
					ecs_iter_t itr = ecs_query_iter(world, query);
					while (ecs_query_next(&itr)) {
						position_t* p = ecs_field(&itr, position_t, 0);
						for (std::int32_t i = 0; i < itr.count; ++i) {
							p[i].x += 1.0f;
							g_checksum += static_cast<std::uint64_t>(p[i].x);
						}
					}
				}
			});
			ecs_query_fini(query);
			break;
		}
		case 5: { /* iterate_position_velocity: query Position+Velocity, p += v */
			res.operations = static_cast<std::uint64_t>(n) * config.query_iterations;
			flecs_bulk_spawn(fb, n, nullptr);
			ecs_query_desc_t qd = {};
			qd.terms[0].id = fb.position;
			qd.terms[1].id = fb.velocity;
			ecs_query_t* query = ecs_query_init(world, &qd);
			run_measure(res, config, [&]() {
				for (std::uint32_t it = 0u; it < config.query_iterations; ++it) {
					ecs_iter_t itr = ecs_query_iter(world, query);
					while (ecs_query_next(&itr)) {
						position_t* p = ecs_field(&itr, position_t, 0);
						velocity_t* v = ecs_field(&itr, velocity_t, 1);
						for (std::int32_t i = 0; i < itr.count; ++i) {
							p[i].x += v[i].x;
							g_checksum += static_cast<std::uint64_t>(p[i].x);
						}
					}
				}
			});
			ecs_query_fini(query);
			break;
		}
		case 6: { /* system_update: run a registered system */
			res.operations = static_cast<std::uint64_t>(n) * config.query_iterations;
			flecs_bulk_spawn(fb, n, nullptr);
			ecs_system_desc_t sd = {};
			sd.query.terms[0].id = fb.position;
			sd.query.terms[1].id = fb.velocity;
			sd.callback = flecs_system_update_cb;
			const ecs_entity_t system = ecs_system_init(world, &sd);
			run_measure(res, config, [&]() {
				for (std::uint32_t it = 0u; it < config.query_iterations; ++it) {
					ecs_run(world, system, 0.016f, nullptr);
				}
			});
			break;
		}
		case 7: { /* deferred_spawn: defer n spawns, flush once */
			res.operations = n;
			run_measure(res, config, [&]() {
				ecs_defer_begin(world);
				for (std::uint32_t i = 0u; i < n; ++i) {
					ecs_entity_desc_t ed = {};
					ed.add = fb.pos_vel_ids;
					ecs_entity_init(world, &ed);
				}
				ecs_defer_end(world);
			});
			break;
		}
		default:
			break;
		}

		fb.fini_world();
		results.push_back(std::move(res));
	}

	return results;
}

/* ------------------------------------------------------------------ */
/*  Public suite entry points                                          */
/* ------------------------------------------------------------------ */

int run_skore_suite(const benchmark_config& config, std::vector<benchmark_result>& out_results) {
	out_results.clear();
	const std::vector<benchmark_result> results = run_skore_suite_impl(config);
	if (results.size() != kScenarioCount) {
		return 1;
	}
	out_results = results;
	return 0;
}

int run_flecs_suite(const benchmark_config& config, std::vector<benchmark_result>& out_results) {
	out_results.clear();
	const std::vector<benchmark_result> results = run_flecs_suite_impl(config);
	if (results.size() != kScenarioCount) {
		return 1;
	}
	out_results = results;
	return 0;
}

/* ------------------------------------------------------------------ */
/*  Reporting                                                          */
/* ------------------------------------------------------------------ */

static void print_row(const std::string& name, std::uint64_t ops, double best, double avg, double worst, double per_sec) {
	std::cout << "  " << std::left << std::setw(26) << name << std::right << std::setw(12) << ops << "  " << std::fixed << std::setprecision(6) << std::setw(10) << best << "  "
			  << std::setw(10) << avg << "  " << std::setw(10) << worst << "  " << std::setprecision(0) << std::setw(14) << per_sec << "\n";
}

void print_report(const char* title, const std::vector<benchmark_result>& results) {
	std::cout << "\n=== " << title << " ===\n";
	std::cout << "  " << std::left << std::setw(26) << "scenario" << std::right << std::setw(12) << "ops" << "  " << std::setw(10) << "best(s)" << "  " << std::setw(10) << "avg(s)"
			  << "  " << std::setw(10) << "worst(s)" << "  " << std::setw(14) << "ops/s" << "\n";
	for (const benchmark_result& res : results) {
		print_row(res.name, res.operations, res.best_seconds, res.average_seconds, res.worst_seconds, res.operations_per_second);
	}
}

void print_comparison(const std::vector<benchmark_result>& skore_results, const std::vector<benchmark_result>& flecs_results) {
	std::cout << "\n=== Skore vs Flecs (average wall time) ===\n";
	std::cout << "  " << std::left << std::setw(26) << "scenario" << std::right << std::setw(14) << "skore_avg(s)" << "  " << std::setw(14) << "flecs_avg(s)" << "  "
			  << std::setw(12) << "skore/flecs" << "\n";
	const std::size_t count = std::min(skore_results.size(), flecs_results.size());
	for (std::size_t i = 0; i < count; ++i) {
		const double sk = skore_results[i].average_seconds;
		const double fl = flecs_results[i].average_seconds;
		const double ratio = (fl > 0.0) ? (sk / fl) : 0.0;
		std::cout << "  " << std::left << std::setw(26) << skore_results[i].name << std::right << std::fixed << std::setprecision(6) << std::setw(14) << sk << "  " << std::setw(14)
				  << fl << "  " << std::setprecision(2) << std::setw(10) << ratio << "x" << "\n";
	}
	std::cout << "\n  (ratio > 1.0: Skore slower than Flecs on that scenario)\n";
	std::cout << "  checksum (sanity): " << static_cast<std::uint64_t>(g_checksum) << "\n";
}

int save_results(const char* path, const benchmark_config& config, const std::vector<benchmark_result>& skore_results, const std::vector<benchmark_result>& flecs_results) {
	std::ofstream out(path, std::ios::trunc);
	if (!out) {
		std::cerr << "save_results: cannot open " << path << "\n";
		return 1;
	}

	out << "# skore-ecs-benchmark comparison results\n\n";
	out << "- entities per scenario: " << config.entity_count << "\n";
	out << "- warm-up runs: " << config.warmup_runs << "\n";
	out << "- timed runs: " << config.timed_runs << "\n";
	out << "- query/system iterations per timed run: " << config.query_iterations << "\n";
	out << "- checksum: " << static_cast<std::uint64_t>(g_checksum) << "\n\n";

	out << "| scenario | skore_avg(s) | flecs_avg(s) | skore/flecs | skore_ops/s | flecs_ops/s |\n";
	out << "| --- | ---: | ---: | ---: | ---: | ---: |\n";
	const std::size_t count = std::min(skore_results.size(), flecs_results.size());
	for (std::size_t i = 0; i < count; ++i) {
		const double sk = skore_results[i].average_seconds;
		const double fl = flecs_results[i].average_seconds;
		const double ratio = (fl > 0.0) ? (sk / fl) : 0.0;
		out << "| " << skore_results[i].name << " | " << sk << " | " << fl << " | " << ratio << "x | " << skore_results[i].operations_per_second << " | "
			<< flecs_results[i].operations_per_second << " |\n";
	}
	out.close();
	return 0;
}

} // namespace skore_ecs_benchmark
