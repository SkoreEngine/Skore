# ResourceObject buffer fixtures (APX-183)

On-disk dummy resources used by buffer-field integration tests for
`sk_resource_object_t` / `SK_RESOURCE_FIELD_TYPE_BUFFER`.

## Layout

| File | Role |
| --- | --- |
| `manifest.json` | Catalog of all fixtures (id, buffer state, sizes) |
| `buffer_small.json` + `.bin` | Resource with a small (32-byte) buffer payload |
| `buffer_large.json` + `.bin` | Resource with a large (>1 MiB) buffer payload |
| `buffer_empty.json` + `.bin` | Resource with an **empty** buffer (has-value, size 0) |
| `buffer_absent.json` | Resource whose buffer field is **absent** (no payload file) |

## Buffer states

| State | Meaning for follow-on tests |
| --- | --- |
| `present` | Call `set_buffer` with the loaded payload bytes |
| `empty` | Call `set_buffer(NULL, 0)` — distinct from unset |
| `absent` | Do not set the field; `has_value_on_this_object` is false |

## Large payload pattern

`buffer_large.bin` is 1 048 577 bytes (`1 MiB + 1`). Byte at index `i` is
`(u8)(i & 0xFF)` so tests can spot-check content without a second golden file.

## Loading

Use the harness in `tests/integration/resource_object_fixtures.h`:

- `sk_resource_fixture_dir` — absolute path to this directory
- `sk_resource_fixture_path` — path to a named fixture file
- `sk_resource_fixture_load_payload` — load binary buffer bytes into an allocator

Path resolution works when the test binary runs from `{build}/bin` (CTest
default) via the compile-time `SK_TEST_DATA_DIR` and several relative fallbacks.
