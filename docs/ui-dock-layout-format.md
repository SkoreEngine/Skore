# Dock layout serialization format (APX-315)

**Status:** v1 contract  
**Types:** `plugins/ui/ui.h` (`sk_ui_dock_layout_t`, `sk_ui_dock_layout_node_t`, `sk_ui_dock_layout_float_t`)  
**Current version:** `SK_UI_DOCK_LAYOUT_VERSION` (integer `1`)

This is the on-disk schema for a named dockspace. It is a single JSON object
(pretty-printed by `dock_layout_save_json`). An optional archive writer may
store the same fields inside a self-contained named map `"dock"`. Runtime
`sk_ui_dock_node_t` handles are never persisted.

---

## Version field

The document root has an explicit integer field:

| JSON key | C field | Type | Meaning |
| --- | --- | --- | --- |
| `version` | `sk_ui_dock_layout_t.version` | integer | Format version. No major.minor. |

Writers always emit `version: SK_UI_DOCK_LAYOUT_VERSION`.

### Version policy (normative)

There is no major.minor and no silent field reinterpretation across versions.

| Encountered `version` | Action | Live tree | Notes |
| --- | --- | --- | --- |
| `== SK_UI_DOCK_LAYOUT_VERSION` (1) | Accept | Replaced by the document | Then run mismatch reconciliation (below). |
| `> SK_UI_DOCK_LAYOUT_VERSION` (unknown / newer) | **Reject** | Unchanged (default layout) | Log encountered version and this build's version. Extra JSON keys are ignored only *within* a matching version; a bumped `version` is not extra-key compatible. |
| `< SK_UI_DOCK_LAYOUT_VERSION` (older) | **Reject. Do not migrate.** | Unchanged (default layout) | v1 is the first published format; there is no v0 reader and no upgrade path. |
| missing `version` key | Treat as `0` → older → **reject** | Unchanged | Same log + default-layout fallback. |
| unparseable / corrupt JSON | **Reject** | Unchanged | Non-zero return; do not crash; do not apply a partial tree. |

`sk_ui_dock_layout_version_supported(version)` returns `0` only when
`version == SK_UI_DOCK_LAYOUT_VERSION`.

When a future integer is published, add a row here: either a migrator
(`1 → 2`) or an explicit reject. Until that row exists, older documents stay
rejected.

---

## Document root

| JSON key | C field | Type | Required | Meaning |
| --- | --- | --- | --- | --- |
| `version` | `version` | integer | yes | Format version (above). |
| `id` | `id` | string | yes | Dockspace identifier (e.g. `"editor-main"`). |
| `flags` | `flags` | unsigned integer | yes | `SK_UI_DOCKSPACE_*` bits. |
| `root` | `root` | node object | yes | Binary node tree. |
| `floating` | `floating[]` / `floating_count` | array of floats | yes (may be empty) | Undocked windows. Cap `SK_UI_DOCK_LAYOUT_FLOAT_MAX` (32). |

---

## Node tree

Every node is an object with `kind` of `"leaf"` or `"split"` (C:
`SK_UI_DOCK_LAYOUT_KIND_LEAF` = 0, `SK_UI_DOCK_LAYOUT_KIND_SPLIT` = 1).

Common fields:

| JSON key | C field | Type | Meaning |
| --- | --- | --- | --- |
| `kind` | `kind` | `"leaf"` \| `"split"` | Node kind. |
| `id` | `id` | string | Stable builder label, or a path (`root`, `root/0`, `root/0/1`). |
| `flags` | `flags` | unsigned integer | `SK_UI_DOCK_NODE_*` bits (`CENTRAL`, `NO_TAB_BAR`, …). |

### Split: orientation and ratio

| JSON key | C field | Type | Meaning |
| --- | --- | --- | --- |
| `axis` | `axis` | integer | `sk_ui_dock_split_t`: `0` horizontal (left \| right), `1` vertical (top / bottom). |
| `ratio` | `ratio` | number | First-child fraction of leftover span after the 6pt splitter, in `(0, 1)`. |
| `a` | `child_a` | node object | Child 0 (left or top). |
| `b` | `child_b` | node object | Child 1 (right or bottom). |

The tree is strictly binary. Nested splits express any rectilinear tiling.

### Leaf: tab order, active tab index, window identifiers

| JSON key | C field | Type | Meaning |
| --- | --- | --- | --- |
| `tabs` | `tabs[]` / `tab_count` | string array | Window identifiers in tab order. Cap `SK_UI_DOCK_LEAF_TABS_MAX` (32). |
| `active_index` | `active_index` | unsigned integer | Selected tab; `0` when `tabs` is empty; must be `< tab_count` otherwise. |

Window identifiers are the host strings from `node_set_id` / `widget_editor_window` (`"console"`, `"hierarchy"`). Never persist generation handles.

A writer may also emit `active` as the window id `tabs[active_index]` for
humans. Readers prefer `active_index` when present; otherwise they resolve
`active` against `tabs`. The selected tab is always the integer index in
the C struct.

---

## Floating window rects

Each `floating[]` entry:

| JSON key | C field | Type | Meaning |
| --- | --- | --- | --- |
| `id` | `window_id` | string | Window identifier. |
| `x` | `x` | number | Logical left (points). |
| `y` | `y` | number | Logical top (points). |
| `w` | `w` | number | Logical width (points). |
| `h` | `h` | number | Logical height (points). |
| `z` | `z` | integer | Overlay z (`z_index`, typically ≥ 50). |

---

## Example (v1)

```json
{
  "version": 1,
  "id": "editor-main",
  "flags": 1,
  "root": {
    "kind": "split",
    "axis": 0,
    "ratio": 0.25,
    "id": "root",
    "flags": 0,
    "a": {
      "kind": "leaf",
      "id": "left",
      "flags": 0,
      "tabs": ["hierarchy"],
      "active_index": 0
    },
    "b": {
      "kind": "split",
      "axis": 1,
      "ratio": 0.75,
      "id": "root/1",
      "flags": 0,
      "a": {
        "kind": "leaf",
        "id": "central",
        "flags": 1,
        "tabs": ["scene", "game"],
        "active_index": 1
      },
      "b": {
        "kind": "leaf",
        "id": "bottom",
        "flags": 0,
        "tabs": ["console"],
        "active_index": 0
      }
    }
  },
  "floating": [
    { "id": "profiler", "x": 80, "y": 60, "w": 360, "h": 240, "z": 50 }
  ]
}
```

---

## Load / host policy

`dock_layout_load_json` (and any later archive `dock_layout_load`):

1. Parse the document (untrusted JSON). Parse errors → non-zero; no model change.
2. If `sk_ui_dock_layout_version_supported(version) != 0`: log the encountered
   version at warn, return non-zero, **do not** replace the live tree.
3. Hosts treat that non-zero as “use the default layout”: keep the in-memory
   workspace, or run the first-run `dock_builder_*` path. Do not apply a
   partial tree.
4. **Mismatch — dropped window (APX-318).** A serialized window id that is
   neither a live `editor_window` (`find_by_id`) nor `dock_window_register`'d
   is dropped. Its tab is omitted. An emptied leaf or emptied split branch is
   collapsed so no empty tab group or empty split remains: the parent split
   is replaced by the surviving sibling, leftover sibling ratios are
   renormalized (first-child fraction re-clamped to the remaining leftover
   span), and collapse cascades when that replacement empties the next
   parent. Floating entries for the same unknown id are skipped.
5. **Mismatch — unsaved window (APX-318).** After the saved tree is applied
   and emptied nodes have collapsed, each `dock_window_register`'d id that
   has no saved position is placed at its declared default dock target
   (stable node id, CENTER tab). If it declares no target, or the target
   node is gone (including collapsed away), it floats at its declared
   default rect (or `80,60,360,240` when the rect was omitted).
6. Registered-but-not-yet-created ids that *are* named in the document still
   become pending binds (cap 64) so load-then-create works.

A builder session that is still open is an error (session stays open).
Apply never `node_destroy`s host `editor_window` nodes.

Related: `docs/ui-docking-design.md` (model and apply). This file is the
serialization schema.
