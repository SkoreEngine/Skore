# Render graph feature test coverage (APX-154)

Maps the migration audit feature inventory
(`docs/render-graph-migration-audit.md` §1) one-to-one to dedicated
`SK_TEST` names in the `sk-render-graph` plugin.

**Discovery:** tests live under `#ifdef SK_TESTS` in
`plugins/render_graph/render_graph.c` and register via the same
constructor/`sk_plugin_run_tests` mechanism used by the RHI test plugin
(`plugins/test_render_device/`). The host `sk-tests` binary loads each
plugin and runs its suite with the standard test command.

**Phase organization:** test names are prefixed by phase
(`build` / `compile` / `execute`) plus supporting
`arena` / `validate` / API smoke groups.

| Audit feature | Dedicated test name(s) | Phase |
| ------------- | ---------------------- | ----- |
| Pass declaration (all types: Compute / Graphics / Raytrace / Transfer) | `render_graph_build_all_pass_types` | build |
| Pass ordering hints (`Stage`) + topo stage tie-break | `render_graph_compile_topo_stage_order` | compile |
| Pass dependency declaration + edges (linear / diamond / multi-writer) | `render_graph_build_linear_chain`, `render_graph_build_diamond`, `render_graph_build_multiple_writers` | build |
| Single-pass declare + record/stage metadata | `render_graph_build_single_pass` | build |
| Resource kind: Texture | `render_graph_build_all_resource_kinds`, `render_graph_build_single_pass` | build |
| Resource kind: Buffer | `render_graph_build_all_resource_kinds`, `render_graph_buffer_and_view_declare` | build |
| Resource kind: View | `render_graph_build_all_resource_kinds`, `render_graph_buffer_and_view_declare` | build |
| Resource kind: Imported | `render_graph_build_all_resource_kinds`, `render_graph_build_imported_backbuffer` | build |
| Resource kind: Instance (CPU blackboard) | `render_graph_build_all_resource_kinds` | build |
| Imported vs transient resources | `render_graph_build_imported_vs_transient` | build |
| Access: Read | `render_graph_build_access_combinations` | build |
| Access: Write | `render_graph_build_access_combinations` | build |
| Access: ReadWrite | `render_graph_build_access_combinations` | build |
| Resolve attachment (`pass_resolve`) | `render_graph_build_resolve_attachment` | build |
| View → parent lifetime (subresource alias for use intervals) | `render_graph_build_view_extends_parent_lifetime` | build/compile |
| Culling of unused / non-root passes | `render_graph_compile_culls_unused_pass` | compile |
| Never-cull side-effect passes | `render_graph_compile_side_effects_never_cull` | compile |
| Producer chain retained for outputs | `render_graph_compile_keeps_producer_chain` | compile |
| Cycle detection (defined error) | `render_graph_compile_cycle_is_defined_error` | compile |
| Topological order (linear chain) | `render_graph_compile_linear_chain_order` | compile |
| Resource lifetime computation (first/last use, write-only start) | `render_graph_compile_resource_lifetimes` | compile |
| Transient aliasing (disjoint lifetimes share heap offset) | `render_graph_compile_alias_disjoint_lifetimes` | compile |
| Aliased resources never overlap in time (same mem range) | `render_graph_compile_alias_lifetimes_never_overlap` | compile |
| Alias eligibility excludes outputs / ping-pong / persistent | `render_graph_compile_alias_excludes_outputs_and_persistent` | compile |
| Barriers: texture write→read sequence | `render_graph_execute_records_texture_barrier_sequence` | execute |
| Barriers: buffer WAW / same-state hazard | `render_graph_execute_buffer_barriers_and_write_hazard` | execute |
| Barriers: graphics color attachment | `render_graph_execute_graphics_color_attachment_barrier` | execute |
| Barriers: transfer copy src/dst | `render_graph_execute_transfer_copy_barrier` | execute |
| Barriers: read-write (UAV/General) | `render_graph_execute_read_write_access_barrier` | execute |
| Alias activation memory barriers | `render_graph_execute_alias_emits_memory_barriers` | execute |
| Imported state restore after execute | `render_graph_execute_imported_restores_state` | execute |
| Record callbacks invoked in compile order | `render_graph_execute_invokes_record_callbacks` | execute |
| RHI mock end-to-end (create + barriers + record) | `render_graph_execute_with_rhi_mock_end_to_end` | execute |
| RHI mock alias heaps + memory barriers | `render_graph_execute_with_rhi_mock_alias_and_barriers` | execute |
| Multi-queue / async paths | **N/A in legacy** (audit §1.7, §1.10). Documented by `render_graph_execute_single_queue_no_async` | execute |
| Empty / degenerate graphs | `render_graph_compile_empty_graph`, `render_graph_execute_empty_graph` | compile/execute |
| Validation: duplicate resource name | `render_graph_validate_duplicate_resource_name` | build |
| Validation: write imported read-only | `render_graph_validate_write_imported_read_only` | build |
| Validation: outside frame / stale pass | `render_graph_validate_outside_frame_and_pass_scope` | build |
| Validation: capacity exceeded (defined error) | `render_graph_validate_capacity_exceeded` | build |
| Validation: unknown resource dependency | `render_graph_validate_unknown_resource_dep` | build |
| Frame memory: arena alignment | `render_graph_arena_alignment` | memory |
| Frame memory: begin resets arena | `render_graph_arena_reset_semantics` | memory |
| Frame memory: high-water tracking | `render_graph_arena_high_water_tracking` | memory |
| Frame memory: sub-arena rollback | `render_graph_arena_sub_arena_rollback` | memory |
| Frame memory: growth only outside frame | `render_graph_capacity_growth_outside_frame` | memory |
| Frame memory: mid-frame OOS is defined error | `render_graph_out_of_space_is_defined_error` | memory |
| Zero heap during build phase | `render_graph_build_phase_zero_heap_allocs` | build |
| Zero heap during compile phase | `render_graph_compile_zero_heap_allocs` | compile |
| Zero heap steady-state execute after warm-up | `render_graph_execute_zero_heap_after_warmup` | execute |
| Instrumented heap hook (counting allocator) | `render_graph_heap_allocator_hook_installs` | memory |
| Instrumented zero-heap N-frame steady state | `render_graph_instrumented_zero_heap_steady_state` | memory |
| Instrumented high-water + growth flat | `render_graph_instrumented_high_water_and_growth_flat` | memory |
| Instrumented capacity retained after worst-case | `render_graph_instrumented_capacity_retained_after_worst_case` | memory |
| Instrumented mid-frame OOS (no silent malloc) | `render_graph_instrumented_mid_frame_capacity_error` | memory |
| Instrumented arena reset between frames | `render_graph_instrumented_arena_reset_between_frames` | memory |
| API table completeness + type id | `render_graph_api_table_is_complete`, `render_graph_api_type_id_nonzero` | smoke |
| Create/destroy defaults + begin pool reset | `render_graph_create_destroy_defaults`, `render_graph_begin_resets_pools_and_marks_in_frame` | smoke |

## Notes

- AccelerationStructure resource kind from C++ main is **not** on the C API
  (`sk_rg_resource_kind_t`); no test is required until it is ported.
- Scene/camera/auto-descriptor conveniences (audit §1.8) are deferred
  conveniences; not part of the core build/compile/execute inventory for
  this suite.
- Multi-queue/async is intentionally **absent** (matches legacy); the
  dedicated test encodes the single-queue contract rather than exercising
  a non-existent API.
