# Foundation contract sketches (APX-278)

These headers are the **pinned public contract** for the `app/` + `core/` merge
into `foundation/`. They are sketches: they are **not** on the compile include
path of today's `sk-core` / `sk-app` targets.

Later implementation tasks **copy/adapt** these files into `foundation/` when
that module is created (see `docs/foundation-refactor-design.md`). Do not
implement `.c` sources or CMake from this folder.

| Sketch | Becomes |
|---|---|
| `app.h` | `foundation/app.h` |
| `logger.h` | `foundation/logger.h` |
| `plugin.h` | `foundation/plugin.h` |
| `repository.h` | delta applied onto today's `core/repository.h` → `foundation/repository.h` |
| `filesystem.h` | delta applied onto today's `core/filesystem.h` → `foundation/filesystem.h` |
| `resource_assets.h` | delta applied onto today's `core/resource_assets.h` → `foundation/resource_assets.h` |
| `resource_serialize.h` | delta: every serialize/deserialize gains `repo_api`; file helpers gain `fs` |
| `internal/tables.h` | `foundation/internal/tables.h` — install hooks, not public |
| `internal/filesystem_context.h` | `foundation/internal/filesystem_context.h` |

Include style after the move: `#include "app.h"` (CMake `PUBLIC` include dir =
`foundation/`). Not `#include <foundation/app.h>`.

Removed from the contract (must not reappear):

- `sk_app_api(void)`
- `sk_logger_api(void)` / `sk_logger_bind_api` / `sk_logger_get_api`
- `sk_filesystem_api(void)` / `sk_filesystem_get_api`
- `sk_platform_api(void)` / `sk_platform_get_api`
- `sk_repository_api(void)`
- `sk_resource_assets_api(void)`
- `sk_app_destroy` (replaced by `sk_app_shutdown`)
