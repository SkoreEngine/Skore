/**
 * @file editor_shell.c
 * @brief v2 editor shell (APX-366) implementation.
 *
 * The shell owns one sk-ui context that hosts the application frame (menu
 * bar, workspace switcher, toolbar, dock host) and the active workspace's
 * dockspace model. Window open/close/focus are driven through the
 * struct-table registries — sk_editor_window_open / sk_editor_window_close
 * over the SK_EDITOR_WINDOW_IMPL_TYPE_ID impls, the Project Browser through
 * its APX-365 ops table — and menu items that toggle windows route through
 * those tables, never direct symbols.
 *
 * The menu bar mirrors the C++ editor's MenuItemContext surface (File /
 * Edit / Build / Tools / Window / Help); features that are not implemented
 * yet stay inert or mocked instead of being dropped. Graph node editor menus
 * are not ported (migration manifest §4).
 *
 * Only the active workspace's dock model is live at a time: switching tears
 * the previous model down (sk_editor_workspace_clear_dockspace) and rebuilds
 * the new one on the shared context, because window chrome ids are
 * process-global.
 */

#include "editor_shell.h"

#include "allocator.h"
#include "editor_layout.h"
#include "logger.h"
#include "window_ops.h"
#include "windows/console_window.h"
#include "windows/entity_tree_window.h"
#include "windows/project_browser_window.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define SHELL_MENU_ITEM_CAP 64u
#define SHELL_MENU_NODE_CAP 128u
#define SHELL_MENU_CHILD_CAP 12u
#define SHELL_WS_CAP 8u
#define SHELL_WS_TYPE_CAP 8u
#define SHELL_TOOLBAR_CAP 16u
#define SHELL_WINDOW_CAP 32u
#define SHELL_ITEM_ID_CAP 96u
#define SHELL_TMP_ID_CAP 64u

#define SHELL_U32_MAX 0xFFFFFFFFu

/* C++ MenuItemContext inserts a separator when a child priority jumps >= 50. */
#define SHELL_MENU_SEPARATOR_GAP 50

typedef struct shell_menu_node_t {
	sk_ui_node_t node;	/* widget_menu / widget_submenu / widget_menu_item */
	sk_ui_node_t popup; /* menu_popup child (menus / submenus only) */
	u32 parent;			/* index into nodes, or 0 for the pseudo root */
	u32 item_index;		/* index into shell->items for leaf items, SHELL_U32_MAX for menus */
	i32 priority;
	u32 children[SHELL_MENU_CHILD_CAP];
	u32 child_count;
	u32 seq;
	const_chr_t label; /* -> label_buf (node-owned copy) */
	char label_buf[SHELL_TMP_ID_CAP];
	char id[SHELL_ITEM_ID_CAP];
} shell_menu_node_t;

typedef struct shell_toolbar_button_t {
	const_chr_t id;
	const_chr_t label;
	void (*action)(sk_app_context_t* app_context, const sk_app_api_t* app_api, void* user);
	i32 (*enabled)(sk_app_context_t* app_context, const sk_app_api_t* app_api, void* user);
	void* user;
} shell_toolbar_button_t;

typedef struct shell_ws_entry_t {
	sk_editor_workspace_t* ws;
	u32 type_id;
	sk_ui_node_t tab;
} shell_ws_entry_t;

typedef struct shell_ws_type_item_t {
	u32 type_id;
	const_chr_t name;
	sk_ui_node_t node;
} shell_ws_type_item_t;

struct sk_editor_shell_t {
	sk_app_context_t* app_context;
	const sk_app_api_t* app_api;
	const sk_ui_api_t* ui;
	sk_ui_context_t* ctx;
	const sk_logger_api_t* logger_api;
	sk_logger_context_t* log_ctx;
	sk_logger_t* log;

	/* borrowed host font params for the per-frame paint call (may be NULL) */
	sk_ui_font_system_t* fonts;
	sk_ui_font_t* font;

	/* frame nodes */
	sk_ui_node_t menu_bar;
	sk_ui_node_t toolbar;
	sk_ui_node_t dock_host;
	sk_ui_node_t ws_tabs;
	sk_ui_node_t ws_add;
	sk_ui_node_t ws_add_popup;

	/* menu registry (tree rebuilt from items when menu_dirty) */
	sk_editor_shell_menu_item_t items[SHELL_MENU_ITEM_CAP];
	u32 item_count;
	shell_menu_node_t nodes[SHELL_MENU_NODE_CAP];
	u32 node_count;
	i32 menu_dirty;

	/* toolbar */
	shell_toolbar_button_t toolbar_buttons[SHELL_TOOLBAR_CAP];
	u32 toolbar_count;
	sk_ui_node_t toolbar_nodes[SHELL_TOOLBAR_CAP];

	/* workspace switcher */
	shell_ws_entry_t workspaces[SHELL_WS_CAP];
	u32 workspace_count;
	u32 ws_tab_version;
	shell_ws_type_item_t ws_types[SHELL_WS_TYPE_CAP];
	u32 ws_type_count;

	/* mocked toolbar state */
	i32 sim_playing;
	i32 debug_options;
};

/* ------------------------------------------------------------------ */
/*  Small helpers                                                     */
/* ------------------------------------------------------------------ */

static void shell_log(const sk_editor_shell_t* shell, const_chr_t fmt, ...) {
	va_list args;
	if (shell == NULL || shell->logger_api == NULL || shell->log == NULL) {
		return;
	}
	va_start(args, fmt);
	sk_log_messagev(shell->logger_api, SK_LOGGER_TYPE_WARN, shell->log, fmt, args);
	va_end(args);
}

/* Build "<parent_id>.<seg_id>" (or "<seg_id>" when parent_id is NULL), bounded
 * by @p cap. Avoids snprintf %s truncation/alias warnings on fixed buffers. */
static void shell_id_build(const_chr_t parent_id, const_chr_t seg, char* out, u32 cap) {
	u32 n = 0u;
	if (cap == 0u) {
		return;
	}
	if (parent_id != NULL) {
		while (*parent_id != '\0' && n + 1u < cap) {
			out[n++] = *parent_id++;
		}
	}
	if (n + 2u < cap) {
		out[n++] = '.';
	}
	while (*seg != '\0' && n + 1u < cap) {
		char c = *seg;
		if (c >= 'A' && c <= 'Z') {
			c = (char)(c - 'A' + 'a');
		} else if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) {
			c = '_';
		}
		out[n++] = c;
		seg++;
	}
	out[n] = '\0';
}

/* ------------------------------------------------------------------ */
/*  Menu registry + tree                                              */
/* ------------------------------------------------------------------ */

static u32 shell_node_find_child(const sk_editor_shell_t* shell, u32 parent_idx, const_chr_t label) {
	const shell_menu_node_t* parent = &shell->nodes[parent_idx];
	u32 i;
	for (i = 0u; i < parent->child_count; ++i) {
		u32 child = parent->children[i];
		if (strcmp(shell->nodes[child].label, label) == 0) {
			return child;
		}
	}
	return SHELL_U32_MAX;
}

static u32 shell_node_ensure(sk_editor_shell_t* shell, u32 parent_idx, const_chr_t label) {
	shell_menu_node_t* parent;
	shell_menu_node_t* node;
	u32 idx;
	idx = shell_node_find_child(shell, parent_idx, label);
	if (idx != SHELL_U32_MAX) {
		return idx;
	}
	if (shell->node_count >= SHELL_MENU_NODE_CAP || parent_idx >= shell->node_count) {
		return SHELL_U32_MAX;
	}
	idx = shell->node_count++;
	node = &shell->nodes[idx];
	memset(node, 0, sizeof(*node));
	(void)snprintf(node->label_buf, sizeof(node->label_buf), "%s", label);
	node->label = node->label_buf;
	node->parent = parent_idx;
	node->item_index = SHELL_U32_MAX;
	node->seq = idx;
	if (parent_idx == 0u) {
		shell_id_build("shell.menu", node->label_buf, node->id, (u32)sizeof(node->id));
	} else {
		parent = &shell->nodes[parent_idx];
		shell_id_build(parent->id, node->label_buf, node->id, (u32)sizeof(node->id));
	}
	parent = &shell->nodes[parent_idx];
	if (parent->child_count < SHELL_MENU_CHILD_CAP) {
		parent->children[parent->child_count++] = idx;
	}
	return idx;
}

/* Lower priority first; ties keep insertion order (seq). */
static i32 shell_node_before(const sk_editor_shell_t* shell, u32 a, u32 b) {
	const shell_menu_node_t* na = &shell->nodes[a];
	const shell_menu_node_t* nb = &shell->nodes[b];
	if (na->priority != nb->priority) {
		return na->priority < nb->priority ? 1 : 0;
	}
	return na->seq < nb->seq ? 1 : 0;
}

static void shell_menu_destroy_widgets(sk_editor_shell_t* shell) {
	const sk_ui_api_t* ui = shell->ui;
	sk_ui_node_t children[SHELL_MENU_NODE_CAP];
	u32 count = ui->node_child_count(shell->ctx, shell->menu_bar);
	u32 i;
	if (count > SHELL_MENU_NODE_CAP) {
		count = SHELL_MENU_NODE_CAP;
	}
	for (i = 0u; i < count; ++i) {
		children[i] = ui->node_child_at(shell->ctx, shell->menu_bar, i);
	}
	for (i = 0u; i < count; ++i) {
		if (sk_ui_node_is_valid(children[i])) {
			(void)ui->node_destroy(shell->ctx, children[i]);
		}
	}
}

// NOLINTBEGIN(misc-no-recursion) -- menu depth is bounded (<= 3 levels: top menu,
// submenu, items); the widget walk mirrors the C++ MenuItemContext Draw recursion.
static void shell_menu_make_nodes(sk_editor_shell_t* shell, u32 idx) {
	const sk_ui_api_t* ui = shell->ui;
	shell_menu_node_t* node = &shell->nodes[idx];
	u32 i;
	i32 last_priority;
	if (node->child_count > 0u) {
		/* menu / submenu */
		if (node->parent == 0u) {
			node->node = ui->widget_menu(shell->ctx, shell->menu_bar, node->label, node->id);
		} else {
			sk_ui_node_t parent_popup = shell->nodes[node->parent].popup;
			node->node = ui->widget_submenu(shell->ctx, parent_popup, node->label, node->id);
		}
		if (!sk_ui_node_is_valid(node->node)) {
			return;
		}
		node->popup = ui->menu_get_popup(shell->ctx, node->node);
		last_priority = shell->nodes[node->children[0]].priority;
		for (i = 0u; i < node->child_count; ++i) {
			u32 child = node->children[i];
			if (i > 0u && last_priority + SHELL_MENU_SEPARATOR_GAP < shell->nodes[child].priority) {
				char sep_label[SHELL_TMP_ID_CAP];
				char sep_id[SHELL_ITEM_ID_CAP];
				(void)snprintf(sep_label, sizeof(sep_label), "sep%u", i);
				shell_id_build(node->id, sep_label, sep_id, (u32)sizeof(sep_id));
				(void)ui->widget_menu_separator(shell->ctx, node->popup, sep_id);
			}
			last_priority = shell->nodes[child].priority;
			shell_menu_make_nodes(shell, child);
		}
		return;
	}
	if (node->item_index == SHELL_U32_MAX) {
		return; /* empty top-level registration with no children and no action */
	}
	{
		sk_ui_node_t parent_popup = shell->nodes[node->parent].popup;
		const sk_editor_shell_menu_item_t* item = &shell->items[node->item_index];
		node->node = ui->widget_menu_item(shell->ctx, parent_popup, node->label, node->id);
		if (sk_ui_node_is_valid(node->node) && item->shortcut != NULL) {
			(void)ui->menu_item_set_shortcut(shell->ctx, node->node, item->shortcut);
		}
	}
}
// NOLINTEND(misc-no-recursion)

static void shell_menus_rebuild(sk_editor_shell_t* shell) {
	u32 i;
	memset(shell->nodes, 0, sizeof(shell->nodes));
	shell->node_count = 1u; /* node 0 = pseudo root */
	for (i = 0u; i < shell->item_count; ++i) {
		const sk_editor_shell_menu_item_t* item = &shell->items[i];
		const_chr_t p = item->item_name;
		u32 cur = 0u;
		while (*p != '\0') {
			const_chr_t seg = p;
			u32 seg_len = 0u;
			while (p[seg_len] != '\0' && p[seg_len] != '/') {
				seg_len++;
			}
			p += seg_len;
			if (seg_len == 0u) {
				if (*p == '/') {
					p++;
				}
				continue;
			}
			{
				/* label is a borrowed pointer into the item string; make it
						 * NUL-terminated by copying the segment into a scratch area
						 * only when it is not already followed by '/'. Since item
						 * strings are process-lifetime and slash-separated, we store
						 * the segment as a pointer and rely on the following '/' or
						 * NUL as the terminator for strcmp. */
				char label_buf[SHELL_TMP_ID_CAP];
				u32 copy_len = seg_len < sizeof(label_buf) - 1u ? seg_len : sizeof(label_buf) - 1u;
				u32 child;
				memcpy(label_buf, seg, copy_len);
				label_buf[copy_len] = '\0';
				child = shell_node_ensure(shell, cur, label_buf);
				if (child == SHELL_U32_MAX) {
					break;
				}
				if (*p != '/') {
					shell->nodes[child].item_index = i;
					shell->nodes[child].priority = item->priority;
				}
				cur = child;
			}
			if (*p == '/') {
				p++;
			}
		}
	}
	/* stable-ish sort of each menu's children by (priority, insertion order) */
	for (i = 1u; i < shell->node_count; ++i) {
		shell_menu_node_t* node = &shell->nodes[i];
		u32 a;
		for (a = 1u; a < node->child_count; ++a) {
			u32 key = node->children[a];
			u32 j = a;
			while (j > 0u && shell_node_before(shell, key, node->children[j - 1u])) {
				node->children[j] = node->children[j - 1u];
				j--;
			}
			node->children[j] = key;
		}
	}
	shell_menu_destroy_widgets(shell);
	for (i = 0u; i < shell->nodes[0].child_count; ++i) {
		shell_menu_make_nodes(shell, shell->nodes[0].children[i]);
	}
}

static void shell_menus_sync(sk_editor_shell_t* shell) {
	const sk_ui_api_t* ui = shell->ui;
	sk_app_context_t* app_context = shell->app_context;
	const sk_app_api_t* app_api = shell->app_api;
	u32 i;
	for (i = 1u; i < shell->node_count; ++i) {
		shell_menu_node_t* node = &shell->nodes[i];
		const sk_editor_shell_menu_item_t* item;
		i32 enabled;
		if (!sk_ui_node_is_valid(node->node)) {
			continue;
		}
		if (node->item_index == SHELL_U32_MAX) {
			(void)ui->menu_set_enabled(shell->ctx, node->node, 1);
			continue;
		}
		item = &shell->items[node->item_index];
		enabled = item->enabled != NULL ? item->enabled(app_context, app_api, item->user) : 1;
		(void)ui->menu_item_set_enabled(shell->ctx, node->node, enabled != 0 ? 1 : 0);
		if (item->selected != NULL) {
			(void)ui->menu_item_set_selected(shell->ctx, node->node, item->selected(app_context, app_api, item->user) != 0 ? 1 : 0);
		}
		if (item->visible != NULL) {
			(void)ui->node_set_prop_i32(shell->ctx, node->node, "hidden", item->visible(app_context, app_api, item->user) != 0 ? 0 : 1);
		}
		if (enabled != 0 && ui->menu_item_clicked(shell->ctx, node->node) != 0 && item->action != NULL) {
			item->action(app_context, app_api, item->user);
		}
	}
}

void sk_editor_shell_add_menu_item(sk_editor_shell_t* shell, const sk_editor_shell_menu_item_t* item) {
	if (shell == NULL || item == NULL || item->item_name == NULL || item->item_name[0] == '\0') {
		return;
	}
	if (shell->item_count < SHELL_MENU_ITEM_CAP) {
		shell->items[shell->item_count] = *item;
		shell->item_count++;
		shell->menu_dirty = 1;
	}
}

/* ------------------------------------------------------------------ */
/*  Toolbar                                                           */
/* ------------------------------------------------------------------ */

static void shell_toolbar_build(sk_editor_shell_t* shell) {
	const sk_ui_api_t* ui = shell->ui;
	u32 i;
	for (i = 0u; i < shell->toolbar_count; ++i) {
		const shell_toolbar_button_t* btn = &shell->toolbar_buttons[i];
		shell->toolbar_nodes[i] = ui->widget_button(shell->ctx, shell->toolbar, btn->label, btn->id);
	}
}

static void shell_toolbar_sync(sk_editor_shell_t* shell) {
	const sk_ui_api_t* ui = shell->ui;
	sk_app_context_t* app_context = shell->app_context;
	const sk_app_api_t* app_api = shell->app_api;
	u32 i;
	for (i = 0u; i < shell->toolbar_count; ++i) {
		const shell_toolbar_button_t* btn = &shell->toolbar_buttons[i];
		sk_ui_node_t node = shell->toolbar_nodes[i];
		i32 enabled;
		if (!sk_ui_node_is_valid(node)) {
			continue;
		}
		enabled = btn->enabled != NULL ? btn->enabled(app_context, app_api, btn->user) : 1;
		if (enabled != 0 && btn->action != NULL && ui->button_clicked(shell->ctx, node) != 0) {
			btn->action(app_context, app_api, btn->user);
		}
	}
}

/* ------------------------------------------------------------------ */
/*  Workspace switcher                                                */
/* ------------------------------------------------------------------ */

static const_chr_t shell_ws_type_name(const sk_editor_shell_t* shell, u32 type_id) {
	u32 i;
	for (i = 0u; i < shell->ws_type_count; ++i) {
		if (shell->ws_types[i].type_id == type_id) {
			return shell->ws_types[i].name;
		}
	}
	return NULL;
}

static void shell_ws_tabs_destroy(sk_editor_shell_t* shell) {
	const sk_ui_api_t* ui = shell->ui;
	sk_ui_node_t children[SHELL_WS_CAP + 2u];
	u32 count = ui->node_child_count(shell->ctx, shell->ws_tabs);
	u32 i;
	if (count > SHELL_WS_CAP + 2u) {
		count = SHELL_WS_CAP + 2u;
	}
	for (i = 0u; i < count; ++i) {
		children[i] = ui->node_child_at(shell->ctx, shell->ws_tabs, i);
	}
	for (i = 0u; i < count; ++i) {
		if (sk_ui_node_is_valid(children[i])) {
			(void)ui->node_destroy(shell->ctx, children[i]);
		}
	}
}

static void shell_ws_tabs_build(sk_editor_shell_t* shell) {
	const sk_ui_api_t* ui = shell->ui;
	sk_editor_workspace_t* active = sk_editor_workspace_active(shell->app_context, shell->app_api);
	u32 i;
	shell_ws_tabs_destroy(shell);
	for (i = 0u; i < shell->workspace_count; ++i) {
		shell_ws_entry_t* entry = &shell->workspaces[i];
		const_chr_t name = shell_ws_type_name(shell, entry->type_id);
		char id[SHELL_TMP_ID_CAP];
		entry->tab = SK_UI_NODE_INVALID;
		if (name == NULL) {
			continue;
		}
		(void)snprintf(id, sizeof(id), "shell.ws.tab.%u", entry->type_id);
		entry->tab = ui->widget_selection_button(shell->ctx, shell->ws_tabs, name, entry->ws == active, id, 0.0f, 0.0f);
	}
	shell->ws_add = ui->widget_button(shell->ctx, shell->ws_tabs, "+", "shell.ws.add");
	shell->ws_add_popup = ui->widget_menu_popup(shell->ctx, shell->ws_add, 0, "shell.ws.add_popup");
	for (i = 0u; i < shell->ws_type_count; ++i) {
		shell_ws_type_item_t* item = &shell->ws_types[i];
		char id[SHELL_TMP_ID_CAP];
		(void)snprintf(id, sizeof(id), "shell.ws.add.%u", item->type_id);
		item->node = ui->widget_menu_item(shell->ctx, shell->ws_add_popup, item->name, id);
	}
}

static void shell_ws_tabs_sync(sk_editor_shell_t* shell) {
	const sk_ui_api_t* ui = shell->ui;
	sk_editor_workspace_t* active = sk_editor_workspace_active(shell->app_context, shell->app_api);
	u32 i;
	if (shell->ws_tab_version != shell->workspace_count) {
		shell_ws_tabs_build(shell);
		shell->ws_tab_version = shell->workspace_count;
	}
	for (i = 0u; i < shell->workspace_count; ++i) {
		shell_ws_entry_t* entry = &shell->workspaces[i];
		if (sk_ui_node_is_valid(entry->tab)) {
			(void)ui->button_set_selected(shell->ctx, entry->tab, entry->ws == active);
		}
	}
	for (i = 0u; i < shell->workspace_count; ++i) {
		if (sk_ui_node_is_valid(shell->workspaces[i].tab) && ui->button_clicked(shell->ctx, shell->workspaces[i].tab) != 0) {
			(void)sk_editor_shell_switch_workspace(shell, shell->workspaces[i].type_id);
			return;
		}
	}
	if (ui->button_clicked(shell->ctx, shell->ws_add) != 0) {
		(void)ui->menu_set_open(shell->ctx, shell->ws_add_popup, 1);
	}
	for (i = 0u; i < shell->ws_type_count; ++i) {
		if (sk_ui_node_is_valid(shell->ws_types[i].node) && ui->menu_item_clicked(shell->ctx, shell->ws_types[i].node) != 0) {
			(void)sk_editor_shell_switch_workspace(shell, shell->ws_types[i].type_id);
			(void)ui->menu_set_open(shell->ctx, shell->ws_add_popup, 0);
			return;
		}
	}
}

/* ------------------------------------------------------------------ */
/*  Dock host + window registry plumbing                              */
/* ------------------------------------------------------------------ */

static void shell_sync_dock_host(sk_editor_shell_t* shell) {
	const sk_ui_api_t* ui = shell->ui;
	sk_editor_workspace_t* ws = sk_editor_workspace_active(shell->app_context, shell->app_api);
	sk_ui_dock_node_t root;
	sk_ui_node_t host;
	if (ws == NULL) {
		return;
	}
	if (sk_editor_workspace_dock_context(ws) != shell->ctx) {
		return;
	}
	root = sk_editor_workspace_dock_root(ws);
	if (!sk_ui_dock_node_is_valid(root)) {
		return;
	}
	host = ui->dockspace_host_node(shell->ctx, root);
	if (!sk_ui_node_is_valid(host)) {
		return;
	}
	if (!sk_ui_node_eq(ui->node_parent(shell->ctx, host), shell->dock_host)) {
		sk_ui_style_props_t p;
		(void)ui->node_reparent(shell->ctx, host, shell->dock_host, (u32)-1);
		memset(&p, 0, sizeof(p));
		p.mask = SK_UI_SP_FLEX_GROW | SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_MIN_WIDTH | SK_UI_SP_MIN_HEIGHT;
		p.layout.flex_grow = 1.0f;
		p.layout.width = sk_ui_percent(100.0f);
		p.layout.height = sk_ui_percent(100.0f);
		p.layout.min_width = sk_ui_pt(1.0f);
		p.layout.min_height = sk_ui_pt(1.0f);
		(void)ui->node_merge_inline_style(shell->ctx, host, &p);
	}
}

static void shell_run_window_draws(sk_editor_shell_t* shell) {
	sk_editor_window_t* windows[SHELL_WINDOW_CAP];
	u32 count = sk_editor_window_iterate(shell->app_context, shell->app_api, windows, SHELL_WINDOW_CAP);
	u32 i;
	if (count > SHELL_WINDOW_CAP) {
		count = SHELL_WINDOW_CAP;
	}
	for (i = 0u; i < count; ++i) {
		sk_editor_window_t* window = windows[i];
		i32 open = 1;
		if (window->draw != NULL) {
			window->draw(window, &open);
		}
		if (open == 0) {
			sk_editor_window_close(shell->app_context, shell->app_api, window);
		}
	}
}

void sk_editor_shell_focus_window(sk_editor_shell_t* shell, sk_editor_window_t* window) {
	const sk_ui_api_t* ui;
	sk_editor_workspace_t* ws;
	sk_ui_context_t* dock_ctx;
	if (shell == NULL || window == NULL || window->dock_id == NULL) {
		return;
	}
	ui = shell->ui;
	ws = sk_editor_workspace_active(shell->app_context, shell->app_api);
	dock_ctx = ws != NULL ? sk_editor_workspace_dock_context(ws) : NULL;
	if (dock_ctx == NULL) {
		return;
	}
	if (ui->dock_tab_set_active(dock_ctx, window->dock_id) != 0) {
		/* Open in the registry but not present in the active workspace's
		 * model (e.g. it was opened for another workspace): dock it at its
		 * default zone, then activate. */
		if (sk_editor_workspace_dock_window(ws, window) == 0) {
			(void)ui->dock_tab_set_active(dock_ctx, window->dock_id);
		}
	}
}

sk_editor_window_t* sk_editor_shell_open_window(sk_editor_shell_t* shell, sk_type_id_t window_type_id) {
	sk_editor_window_t* window;
	if (shell == NULL) {
		return NULL;
	}
	window = sk_editor_window_by_type(shell->app_context, shell->app_api, window_type_id);
	if (window == NULL) {
		if (SK_TYPE_ID_EQ(window_type_id, SK_EDITOR_WINDOW_PROJECT_BROWSER)) {
			/* Through the registered ops table (APX-365), never a direct call. */
			const sk_editor_project_browser_ops_t* ops = sk_editor_project_browser_ops(shell->app_context, shell->app_api);
			if (ops != NULL && ops->open != NULL) {
				window = ops->open(shell->app_context, shell->app_api);
			}
		} else if (SK_TYPE_ID_EQ(window_type_id, SK_EDITOR_WINDOW_CONSOLE)) {
			/* Console toggles route through its registered ops table too. */
			const sk_editor_console_ops_t* ops = sk_editor_console_ops(shell->app_context, shell->app_api);
			if (ops != NULL && ops->open != NULL) {
				window = ops->open(shell->app_context, shell->app_api);
			}
		} else if (SK_TYPE_ID_EQ(window_type_id, SK_EDITOR_WINDOW_ENTITY_TREE)) {
			/* Entity Tree toggles route through its registered ops table too. */
			const sk_editor_entity_tree_ops_t* ops = sk_editor_entity_tree_ops(shell->app_context, shell->app_api);
			if (ops != NULL && ops->open != NULL) {
				window = ops->open(shell->app_context, shell->app_api);
			}
		}
		if (window == NULL) {
			window = sk_editor_window_open(shell->app_context, shell->app_api, window_type_id);
		}
	}
	if (window != NULL) {
		sk_editor_shell_focus_window(shell, window);
	}
	return window;
}

void sk_editor_shell_close_window(sk_editor_shell_t* shell, sk_editor_window_t* window) {
	if (shell == NULL || window == NULL) {
		return;
	}
	sk_editor_window_close(shell->app_context, shell->app_api, window);
}

sk_editor_workspace_t* sk_editor_shell_switch_workspace(sk_editor_shell_t* shell, u32 workspace_type_id) {
	sk_editor_workspace_t* ws = NULL;
	sk_editor_workspace_t* old;
	u32 i;
	if (shell == NULL) {
		return NULL;
	}
	for (i = 0u; i < shell->workspace_count; ++i) {
		if (shell->workspaces[i].type_id == workspace_type_id) {
			ws = shell->workspaces[i].ws;
			break;
		}
	}
	if (ws == NULL) {
		ws = sk_editor_workspace_create(shell->app_context, shell->app_api, workspace_type_id);
		if (ws == NULL) {
			return NULL;
		}
		/* Bind the shared shell context before the first dockspace init. */
		sk_editor_workspace_set_dock_context(ws, shell->ctx);
		if (shell->workspace_count < SHELL_WS_CAP) {
			shell->workspaces[shell->workspace_count].ws = ws;
			shell->workspaces[shell->workspace_count].type_id = workspace_type_id;
			shell->workspaces[shell->workspace_count].tab = SK_UI_NODE_INVALID;
			shell->workspace_count++;
		}
	}
	old = sk_editor_workspace_active(shell->app_context, shell->app_api);
	if (old != ws) {
		if (old != NULL) {
			/* Snapshot the outgoing workspace before tearing its dock model
			 * and swapping the open window set. */
			(void)sk_editor_workspace_capture(old);
			sk_editor_workspace_clear_dockspace(old);
		}
		sk_editor_workspace_switch(ws);
		/* Saved layout if we have one, otherwise the shipped default preset. */
		(void)sk_editor_workspace_restore(ws);
		shell->ws_tab_version = SHELL_U32_MAX;
	} else if (!sk_ui_dock_node_is_valid(sk_editor_workspace_dock_root(ws))) {
		/* The first workspace is created active (workspace_create) before any
		 * model exists; restore saved layout or the default preset. */
		(void)sk_editor_workspace_restore(ws);
		shell->ws_tab_version = SHELL_U32_MAX;
	}
	return ws;
}

/* ------------------------------------------------------------------ */
/*  Frame pipeline + input                                            */
/* ------------------------------------------------------------------ */

static i32 shell_frame_build(sk_editor_shell_t* shell) {
	const sk_ui_api_t* ui = shell->ui;
	sk_ui_node_t root = ui->context_root(shell->ctx);
	sk_ui_node_t top_row;
	sk_ui_node_t ws_tabs;
	sk_ui_style_props_t p;

	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_BACKGROUND_COLOR;
	p.layout.flex_direction = SK_UI_FLEX_COLUMN;
	p.layout.width = sk_ui_percent(100.0f);
	p.layout.height = sk_ui_percent(100.0f);
	p.background_color = sk_ui_rgba(0.075f, 0.08f, 0.10f, 1.0f);
	if (ui->node_set_inline_style(shell->ctx, root, &p) != 0) {
		return -1;
	}

	/* top row: menu bar + spring + workspace switcher (C++ DrawMenu row) */
	top_row = ui->widget_view(shell->ctx, root, "shell.top_row");
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_FLEX_SHRINK | SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_BORDER_WIDTH | SK_UI_SP_BORDER_COLOR;
	p.layout.flex_direction = SK_UI_FLEX_ROW;
	p.layout.width = sk_ui_percent(100.0f);
	p.layout.height = sk_ui_pt(26.0f);
	p.layout.flex_shrink = 0.0f;
	p.layout.border.bottom = 1.0f;
	p.background_color = sk_ui_rgba(0.10f, 0.11f, 0.14f, 1.0f);
	p.border_color = sk_ui_rgba(0.17f, 0.18f, 0.22f, 1.0f);
	if (ui->node_set_inline_style(shell->ctx, top_row, &p) != 0) {
		return -1;
	}
	shell->menu_bar = ui->widget_menu_bar(shell->ctx, top_row, "shell.menu_bar");
	if (!sk_ui_node_is_valid(shell->menu_bar)) {
		return -1;
	}
	/* The menu-bar class fills its parent by default; keep it content-sized so
	 * the spring + workspace switcher share the same row. */
	{
		sk_ui_style_props_t mbp;
		memset(&mbp, 0, sizeof(mbp));
		mbp.mask = SK_UI_SP_WIDTH | SK_UI_SP_FLEX_GROW;
		mbp.layout.width = sk_ui_auto();
		mbp.layout.flex_grow = 0.0f;
		(void)ui->node_set_inline_style(shell->ctx, shell->menu_bar, &mbp);
	}
	(void)ui->widget_spring(shell->ctx, top_row, 1.0f, "shell.top_row.spring");
	ws_tabs = ui->widget_view(shell->ctx, top_row, "shell.ws.tabs");
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_ALIGN_ITEMS | SK_UI_SP_FLEX_SHRINK;
	p.layout.flex_direction = SK_UI_FLEX_ROW;
	p.layout.width = sk_ui_percent(40.0f);
	p.layout.height = sk_ui_percent(100.0f);
	p.layout.align_items = SK_UI_ALIGN_CENTER;
	p.layout.flex_shrink = 0.0f;
	if (ui->node_set_inline_style(shell->ctx, ws_tabs, &p) != 0) {
		return -1;
	}
	shell->ws_tabs = ws_tabs;

	/* toolbar */
	shell->toolbar = ui->widget_view(shell->ctx, root, "shell.toolbar");
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_FLEX_SHRINK | SK_UI_SP_ALIGN_ITEMS | SK_UI_SP_COLUMN_GAP | SK_UI_SP_PADDING |
			 SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_BORDER_WIDTH | SK_UI_SP_BORDER_COLOR;
	p.layout.flex_direction = SK_UI_FLEX_ROW;
	p.layout.width = sk_ui_percent(100.0f);
	p.layout.height = sk_ui_pt(30.0f);
	p.layout.flex_shrink = 0.0f;
	p.layout.align_items = SK_UI_ALIGN_CENTER;
	p.layout.column_gap = 4.0f;
	p.layout.padding.left = 6.0f;
	p.layout.padding.right = 6.0f;
	p.layout.border.bottom = 1.0f;
	p.background_color = sk_ui_rgba(0.085f, 0.09f, 0.12f, 1.0f);
	p.border_color = sk_ui_rgba(0.15f, 0.16f, 0.19f, 1.0f);
	if (ui->node_set_inline_style(shell->ctx, shell->toolbar, &p) != 0) {
		return -1;
	}

	/* dock host: fills everything below the toolbar */
	shell->dock_host = ui->widget_view(shell->ctx, root, "shell.dock_host");
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_FLEX_GROW | SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_MIN_WIDTH | SK_UI_SP_MIN_HEIGHT;
	p.layout.flex_grow = 1.0f;
	p.layout.width = sk_ui_percent(100.0f);
	p.layout.height = sk_ui_percent(100.0f);
	p.layout.min_width = sk_ui_pt(1.0f);
	p.layout.min_height = sk_ui_pt(1.0f);
	if (ui->node_set_inline_style(shell->ctx, shell->dock_host, &p) != 0) {
		return -1;
	}
	return 0;
}

i32 sk_editor_shell_frame(sk_editor_shell_t* shell, f32 width, f32 height, f32 scale_x, f32 scale_y) {
	const sk_ui_api_t* ui;
	sk_ui_paint_params_t paint;
	if (shell == NULL || shell->ui == NULL || shell->ctx == NULL) {
		return -1;
	}
	ui = shell->ui;
	if (shell->menu_dirty != 0) {
		shell_menus_rebuild(shell);
		shell->menu_dirty = 0;
	}
	shell_menus_sync(shell);
	shell_toolbar_sync(shell);
	shell_ws_tabs_sync(shell);
	shell_run_window_draws(shell);
	shell_sync_dock_host(shell);

	if (ui->style_resolve(shell->ctx) != 0) {
		return -1;
	}
	if (ui->layout(shell->ctx, width > 1.0f ? width : 1.0f, height > 1.0f ? height : 1.0f) != 0) {
		return -1;
	}
	if (ui->layout_apply_scale(shell->ctx, scale_x > 0.0f ? scale_x : 1.0f, scale_y > 0.0f ? scale_y : 1.0f) != 0) {
		return -1;
	}
	memset(&paint, 0, sizeof(paint));
	paint.font_system = shell->fonts;
	paint.font = shell->font;
	if (ui->paint(shell->ctx, &paint) != 0) {
		return -1;
	}
	return 0;
}

void sk_editor_shell_pointer(sk_editor_shell_t* shell, f32 x, f32 y, i32 button, i32 down) {
	sk_ui_input_event_t ev;
	if (shell == NULL || shell->ui == NULL || shell->ctx == NULL) {
		return;
	}
	memset(&ev, 0, sizeof(ev));
	ev.x = x;
	ev.y = y;
	if (button >= 0) {
		ev.kind = SK_UI_INPUT_POINTER_BUTTON;
		ev.button = button;
		ev.down = down != 0 ? 1 : 0;
	} else {
		ev.kind = SK_UI_INPUT_POINTER_MOVE;
	}
	(void)shell->ui->input_dispatch(shell->ctx, &ev);
}

void sk_editor_shell_key(sk_editor_shell_t* shell, i32 key, i32 down, u32 mods) {
	sk_ui_input_event_t ev;
	if (shell == NULL || shell->ui == NULL || shell->ctx == NULL) {
		return;
	}
	memset(&ev, 0, sizeof(ev));
	ev.kind = SK_UI_INPUT_KEY;
	ev.key = key;
	ev.down = down != 0 ? 1 : 0;
	ev.mods = mods;
	(void)shell->ui->input_dispatch(shell->ctx, &ev);
}

void sk_editor_shell_text(sk_editor_shell_t* shell, const_chr_t utf8) {
	sk_ui_input_event_t ev;
	if (shell == NULL || shell->ui == NULL || shell->ctx == NULL || utf8 == NULL) {
		return;
	}
	memset(&ev, 0, sizeof(ev));
	ev.kind = SK_UI_INPUT_TEXT;
	ev.text = utf8;
	(void)shell->ui->input_dispatch(shell->ctx, &ev);
}

void sk_editor_shell_scroll(sk_editor_shell_t* shell, f32 offset_x, f32 offset_y) {
	sk_ui_input_event_t ev;
	if (shell == NULL || shell->ui == NULL || shell->ctx == NULL) {
		return;
	}
	memset(&ev, 0, sizeof(ev));
	ev.kind = SK_UI_INPUT_WHEEL;
	ev.scroll_x = offset_x;
	ev.scroll_y = offset_y;
	(void)shell->ui->input_dispatch(shell->ctx, &ev);
}

/* ------------------------------------------------------------------ */
/*  Default menu + toolbar surface (C++ editor entries)               */
/* ------------------------------------------------------------------ */

static void shell_act_mock(sk_app_context_t* app_context, const sk_app_api_t* app_api, void* user) {
	(void)app_context;
	(void)app_api;
	shell_log((const sk_editor_shell_t*)user, "action not implemented yet (inert/mock)");
}

static void shell_act_exit(sk_app_context_t* app_context, const sk_app_api_t* app_api, void* user) {
	(void)app_api;
	(void)user;
	app_api->request_shutdown(app_context);
}

static void shell_act_reset_layout(sk_app_context_t* app_context, const sk_app_api_t* app_api, void* user) {
	sk_editor_shell_t* shell = (sk_editor_shell_t*)user;
	sk_editor_workspace_t* ws = sk_editor_workspace_active(app_context, app_api);
	(void)shell;
	if (ws != NULL) {
		sk_editor_workspace_reset_to_preset(ws);
	}
}

static i32 shell_enabled_false(sk_app_context_t* app_context, const sk_app_api_t* app_api, void* user) {
	(void)app_context;
	(void)app_api;
	(void)user;
	return 0;
}

/* Window toggles: through the registered tables (sk_editor_shell_open_window). */
static void shell_act_open_project_browser(sk_app_context_t* app_context, const sk_app_api_t* app_api, void* user) {
	(void)sk_editor_shell_open_window((sk_editor_shell_t*)user, SK_EDITOR_WINDOW_PROJECT_BROWSER);
	(void)app_context;
	(void)app_api;
}

static void shell_act_open_entity_tree(sk_app_context_t* app_context, const sk_app_api_t* app_api, void* user) {
	(void)sk_editor_shell_open_window((sk_editor_shell_t*)user, SK_EDITOR_WINDOW_ENTITY_TREE);
	(void)app_context;
	(void)app_api;
}

static void shell_act_open_scene_view(sk_app_context_t* app_context, const sk_app_api_t* app_api, void* user) {
	(void)sk_editor_shell_open_window((sk_editor_shell_t*)user, SK_EDITOR_WINDOW_SCENE_VIEW);
	(void)app_context;
	(void)app_api;
}

static void shell_act_open_properties(sk_app_context_t* app_context, const sk_app_api_t* app_api, void* user) {
	(void)sk_editor_shell_open_window((sk_editor_shell_t*)user, SK_EDITOR_WINDOW_PROPERTIES);
	(void)app_context;
	(void)app_api;
}

static void shell_act_open_history(sk_app_context_t* app_context, const sk_app_api_t* app_api, void* user) {
	(void)sk_editor_shell_open_window((sk_editor_shell_t*)user, SK_EDITOR_WINDOW_HISTORY);
	(void)app_context;
	(void)app_api;
}

static void shell_act_open_resource_debugger(sk_app_context_t* app_context, const sk_app_api_t* app_api, void* user) {
	(void)sk_editor_shell_open_window((sk_editor_shell_t*)user, SK_EDITOR_WINDOW_RESOURCE_DEBUGGER);
	(void)app_context;
	(void)app_api;
}

static void shell_act_open_packages(sk_app_context_t* app_context, const sk_app_api_t* app_api, void* user) {
	(void)sk_editor_shell_open_window((sk_editor_shell_t*)user, SK_EDITOR_WINDOW_PACKAGES);
	(void)app_context;
	(void)app_api;
}

static void shell_act_open_settings(sk_app_context_t* app_context, const sk_app_api_t* app_api, void* user) {
	(void)sk_editor_shell_open_window((sk_editor_shell_t*)user, SK_EDITOR_WINDOW_SETTINGS);
	(void)app_context;
	(void)app_api;
}

/* Toolbar actions (mock where the feature is not implemented yet). */
static void shell_act_save_all(sk_app_context_t* app_context, const sk_app_api_t* app_api, void* user) {
	(void)app_context;
	(void)app_api;
	shell_log((const sk_editor_shell_t*)user, "Save All not implemented yet");
}

static void shell_act_play(sk_app_context_t* app_context, const sk_app_api_t* app_api, void* user) {
	(void)app_context;
	(void)app_api;
	((sk_editor_shell_t*)user)->sim_playing = 1;
}

static void shell_act_pause(sk_app_context_t* app_context, const sk_app_api_t* app_api, void* user) {
	(void)app_context;
	(void)app_api;
	((sk_editor_shell_t*)user)->sim_playing = 0;
}

static void shell_act_stop(sk_app_context_t* app_context, const sk_app_api_t* app_api, void* user) {
	(void)app_context;
	(void)app_api;
	((sk_editor_shell_t*)user)->sim_playing = 0;
}

static void shell_register_default_menu(sk_editor_shell_t* shell) {
	sk_editor_shell_menu_item_t item;
	void* user = shell;
#define SHELL_MENU(_path, _priority)                 \
	do {                                             \
		memset(&item, 0, sizeof(item));              \
		item.item_name = _path;                      \
		item.priority = _priority;                   \
		sk_editor_shell_add_menu_item(shell, &item); \
	} while (0)
#define SHELL_MENU_ITEM(_path, _priority, _action, _enabled, _shortcut) \
	do {                                                                \
		memset(&item, 0, sizeof(item));                                 \
		item.item_name = _path;                                         \
		item.priority = _priority;                                      \
		item.action = _action;                                          \
		item.enabled = _enabled;                                        \
		item.shortcut = _shortcut;                                      \
		item.user = user;                                               \
		sk_editor_shell_add_menu_item(shell, &item);                    \
	} while (0)

	/* Top-level menus (priorities mirror C++ Editor.cpp). */
	SHELL_MENU("File", 0);
	SHELL_MENU("Edit", 30);
	SHELL_MENU("Tools", 50);
	SHELL_MENU("Build", 55);
	SHELL_MENU("Window", 60);
	SHELL_MENU("Help", 70);

	/* File */
	SHELL_MENU_ITEM("File/New Project", 0, shell_act_mock, NULL, NULL);
	SHELL_MENU_ITEM("File/Close Project", 10, shell_act_mock, NULL, NULL);
	SHELL_MENU("File/Recent Projects", 20);
	SHELL_MENU_ITEM("File/Recent Projects/No Recent Projects", 0, NULL, shell_enabled_false, NULL);
	SHELL_MENU_ITEM("File/Save All", 1000, shell_act_mock, NULL, "Ctrl+S");
	SHELL_MENU_ITEM("File/Export", 2000, shell_act_mock, NULL, NULL);
	SHELL_MENU_ITEM("File/Export And Run", 2005, shell_act_mock, NULL, NULL);
	SHELL_MENU_ITEM("File/Exit", 2147483647, shell_act_exit, NULL, "Ctrl+Q");

	/* Edit */
	SHELL_MENU_ITEM("Edit/Undo", 10, NULL, shell_enabled_false, "Ctrl+Z");
	SHELL_MENU_ITEM("Edit/Redo", 20, NULL, shell_enabled_false, "Ctrl+Shift+Z");
	SHELL_MENU_ITEM("Edit/Editor Settings", 1000, shell_act_open_settings, NULL, NULL);
	SHELL_MENU_ITEM("Edit/Project Settings", 1010, shell_act_open_settings, NULL, NULL);
	SHELL_MENU_ITEM("Edit/Packages", 1015, shell_act_open_packages, NULL, NULL);

	/* Tools */
	SHELL_MENU_ITEM("Tools/Open Editor", 5, shell_act_mock, NULL, NULL);
	SHELL_MENU_ITEM("Tools/Open C# Project", 7, shell_act_mock, NULL, NULL);
	SHELL_MENU_ITEM("Tools/Create CMake Project", 10, shell_act_mock, NULL, NULL);
	SHELL_MENU_ITEM("Tools/Create C# Project", 15, shell_act_mock, NULL, NULL);
	SHELL_MENU_ITEM("Tools/Reload Shaders", 100, shell_act_mock, NULL, "F5");
	SHELL_MENU_ITEM("Tools/Show Debug Options", 105, shell_act_mock, NULL, NULL);
	SHELL_MENU_ITEM("Tools/Install Skore MCP", 120, shell_act_mock, NULL, NULL);

	/* Build */
	SHELL_MENU_ITEM("Build/Build C# Project", 10, shell_act_mock, NULL, NULL);

	/* Window — window toggles route through the registered tables. */
	SHELL_MENU_ITEM("Window/Project Browser", 0, shell_act_open_project_browser, NULL, NULL);
	SHELL_MENU_ITEM("Window/Entity Tree", 10, shell_act_open_entity_tree, NULL, NULL);
	SHELL_MENU_ITEM("Window/Scene Viewport", 20, shell_act_open_scene_view, NULL, NULL);
	SHELL_MENU_ITEM("Window/Properties", 30, shell_act_open_properties, NULL, NULL);
	SHELL_MENU_ITEM("Window/History", 40, shell_act_open_history, NULL, NULL);
	SHELL_MENU_ITEM("Window/Resource Debugger", 50, shell_act_open_resource_debugger, NULL, NULL);
	SHELL_MENU_ITEM("Window/Reset Layout", 2147483646, shell_act_reset_layout, NULL, NULL);

	/* Help */
	SHELL_MENU_ITEM("Help/Documentation", 10, shell_act_mock, NULL, NULL);
	SHELL_MENU_ITEM("Help/About Skore", 20, shell_act_mock, NULL, NULL);

#undef SHELL_MENU
#undef SHELL_MENU_ITEM
}

static void shell_register_default_toolbar(sk_editor_shell_t* shell) {
	shell_toolbar_button_t btn;
	void* user = shell;
#define SHELL_TOOL(_id, _label, _action, _enabled)              \
	do {                                                        \
		if (shell->toolbar_count < SHELL_TOOLBAR_CAP) {         \
			memset(&btn, 0, sizeof(btn));                       \
			btn.id = _id;                                       \
			btn.label = _label;                                 \
			btn.action = _action;                               \
			btn.enabled = _enabled;                             \
			btn.user = user;                                    \
			shell->toolbar_buttons[shell->toolbar_count] = btn; \
			shell->toolbar_count++;                             \
		}                                                       \
	} while (0)
	SHELL_TOOL("shell.toolbar.save_all", "Save All", shell_act_save_all, NULL);
	SHELL_TOOL("shell.toolbar.undo", "Undo", NULL, shell_enabled_false);
	SHELL_TOOL("shell.toolbar.redo", "Redo", NULL, shell_enabled_false);
	SHELL_TOOL("shell.toolbar.play", "Play", shell_act_play, NULL);
	SHELL_TOOL("shell.toolbar.pause", "Pause", shell_act_pause, NULL);
	SHELL_TOOL("shell.toolbar.stop", "Stop", shell_act_stop, NULL);
	SHELL_TOOL("shell.toolbar.reset_layout", "Reset Layout", shell_act_reset_layout, NULL);
#undef SHELL_TOOL
}

/* ------------------------------------------------------------------ */
/*  Create / destroy                                                  */
/* ------------------------------------------------------------------ */

static void shell_ws_types_capture(sk_editor_shell_t* shell) {
	const sk_allocator_t* alloc = sk_allocator_default();
	const_ptr_t* impls;
	u32 count = shell->app_api->impl_count(shell->app_context, SK_EDITOR_WORKSPACE_IMPL_TYPE_ID);
	u32 i;
	if (count == 0u) {
		return;
	}
	impls = (const_ptr_t*)alloc->alloc(alloc->instance, (size_t)count * sizeof(const_ptr_t));
	if (impls == NULL) {
		return;
	}
	(void)shell->app_api->get_all_impls(shell->app_context, SK_EDITOR_WORKSPACE_IMPL_TYPE_ID, impls, count);
	for (i = 0u; i < count && shell->ws_type_count < SHELL_WS_TYPE_CAP; ++i) {
		const sk_editor_workspace_type_t* type = (const sk_editor_workspace_type_t*)impls[i];
		shell->ws_types[shell->ws_type_count].type_id = type->id;
		shell->ws_types[shell->ws_type_count].name = type->display_name;
		shell->ws_types[shell->ws_type_count].node = SK_UI_NODE_INVALID;
		shell->ws_type_count++;
	}
	alloc->free(alloc->instance, impls);
}

sk_editor_shell_t* sk_editor_shell_create(sk_app_context_t* app_context, const sk_app_api_t* app_api, const sk_ui_api_t* ui) {
	const sk_allocator_t* alloc = sk_allocator_default();
	sk_editor_shell_t* shell;
	if (app_context == NULL || app_api == NULL || ui == NULL) {
		return NULL;
	}
	shell = (sk_editor_shell_t*)alloc->alloc(alloc->instance, sizeof(sk_editor_shell_t));
	if (shell == NULL) {
		return NULL;
	}
	memset(shell, 0, sizeof(*shell));
	shell->app_context = app_context;
	shell->app_api = app_api;
	shell->ui = ui;
	shell->logger_api = app_api->logger_api(app_context);
	shell->log_ctx = app_api->logger_context(app_context);
	if (shell->logger_api != NULL && shell->log_ctx != NULL) {
		shell->log = shell->logger_api->create_logger(shell->log_ctx, "shell");
	}
	shell->ws_tab_version = SHELL_U32_MAX;

	if (ui->init() != 0) {
		sk_editor_shell_destroy(shell);
		return NULL;
	}
	shell->ctx = ui->context_create(NULL);
	if (shell->ctx == NULL) {
		sk_editor_shell_destroy(shell);
		return NULL;
	}
	if (shell_frame_build(shell) != 0) {
		sk_editor_shell_destroy(shell);
		return NULL;
	}
	shell_register_default_menu(shell);
	shell_register_default_toolbar(shell);
	shell_menus_rebuild(shell);
	shell->menu_dirty = 0; /* create-time rebuild above consumed the dirty mark */
	shell_toolbar_build(shell);
	shell_ws_types_capture(shell);
	shell_ws_tabs_build(shell);
	shell->ws_tab_version = shell->workspace_count;

	/* Load EditorLayout.json when present (missing / corrupt → presets). */
	sk_editor_layout_init(app_context, app_api);

	/* Bring up the default workspace (Scene) with its dockspace. */
	(void)sk_editor_shell_switch_workspace(shell, SK_EDITOR_WORKSPACE_SCENE);
	return shell;
}

void sk_editor_shell_destroy(sk_editor_shell_t* shell) {
	if (shell == NULL) {
		return;
	}
	sk_editor_layout_shutdown(shell->app_context, shell->app_api);
	if (shell->logger_api != NULL && shell->log != NULL) {
		shell->logger_api->destroy_logger(shell->log_ctx, shell->log);
		shell->log = NULL;
	}
	if (shell->ui != NULL) {
		if (shell->ctx != NULL) {
			shell->ui->context_destroy(shell->ctx);
			shell->ctx = NULL;
		}
		shell->ui->shutdown();
	}
	sk_allocator_default()->free(sk_allocator_default()->instance, shell);
}

sk_ui_context_t* sk_editor_shell_context(const sk_editor_shell_t* shell) {
	return shell != NULL ? shell->ctx : NULL;
}

void sk_editor_shell_set_fonts(sk_editor_shell_t* shell, sk_ui_font_system_t* fonts, sk_ui_font_t* font) {
	if (shell == NULL) {
		return;
	}
	shell->fonts = fonts;
	shell->font = font;
}

sk_ui_node_t sk_editor_shell_menu_bar(const sk_editor_shell_t* shell) {
	return shell != NULL ? shell->menu_bar : SK_UI_NODE_INVALID;
}

sk_ui_node_t sk_editor_shell_toolbar(const sk_editor_shell_t* shell) {
	return shell != NULL ? shell->toolbar : SK_UI_NODE_INVALID;
}

sk_ui_node_t sk_editor_shell_dock_host(const sk_editor_shell_t* shell) {
	return shell != NULL ? shell->dock_host : SK_UI_NODE_INVALID;
}

sk_ui_node_t sk_editor_shell_find_menu(const sk_editor_shell_t* shell, const_chr_t id) {
	if (shell == NULL || shell->ui == NULL || shell->ctx == NULL || id == NULL) {
		return SK_UI_NODE_INVALID;
	}
	return shell->ui->find_by_id(shell->ctx, id);
}

i32 sk_editor_shell_sim_playing(const sk_editor_shell_t* shell) {
	return shell != NULL ? shell->sim_playing : 0;
}

/* ------------------------------------------------------------------ */
/*  Tests                                                             */
/* ------------------------------------------------------------------ */

#ifdef SK_TESTS

#include "editor_api.h"
#include "filesystem.h"
#include "path.h"
#include "platform.h"
#include "test.h"

typedef struct shell_ui_fixture_t {
	sk_app_boot_t boot;
	const sk_ui_api_t* ui;
	sk_shared_lib_t lib;
} shell_ui_fixture_t;

/* Load the sk-ui shared plugin on a started app context (same plugins-dir
 * resolution as the editor_window.c dock tests). */
static i32 shell_ui_fixture_start(shell_ui_fixture_t* fx) {
	const sk_platform_api_t* plat;
	const sk_filesystem_api_t* fs;
	sk_directory_iterator_t it;
	char base[SK_FS_PATH_MAX];
	char plugins_dir[SK_FS_PATH_MAX];
	char full_path[SK_FS_PATH_MAX];
	char name[SK_FS_PATH_MAX];
	i32 found = 0;
	typedef int (*shell_plugin_entry_fn)(sk_app_context_t* context, const sk_app_api_t* app_api);

	memset(fx, 0, sizeof(*fx));
	fx->boot = sk_app_startup();
	if (fx->boot.context == NULL) {
		return -1;
	}
	plat = fx->boot.api->platform_api(fx->boot.context);
	fs = fx->boot.api->filesystem_api(fx->boot.context);

	if ((fs->app_folder(base, (u32)sizeof(base)) != 0 || base[0] == '\0') && (fs->current_dir(base, (u32)sizeof(base)) != 0 || base[0] == '\0')) {
		return -1;
	}
	if (sk_path_join(sk_str_view_cstr(base), sk_str_view_cstr("plugins"), plugins_dir, (u32)sizeof(plugins_dir)) < 0) {
		return -1;
	}
	it = fs->open_directory(plugins_dir);
	if (it == NULL) {
		return -1;
	}
	while (fs->next_directory(it, name, (u32)sizeof(name)) == 0) {
		if (strncmp(name, "sk-ui.", 6u) != 0 || sk_is_shared_library_filename(name) == 0) {
			continue;
		}
		if (sk_path_join(sk_str_view_cstr(plugins_dir), sk_str_view_cstr(name), full_path, (u32)sizeof(full_path)) < 0) {
			continue;
		}
		fx->lib = plat->lib_open(full_path);
		if (fx->lib == NULL) {
			continue;
		}
		found = 1;
		break;
	}
	fs->close_directory(it);
	if (found == 0) {
		return -1;
	}
	{
		void_ptr_t raw = plat->lib_symbol(fx->lib, "sk_plugin_entry_point");
		shell_plugin_entry_fn entry;
		if (raw == NULL) {
			plat->lib_close(fx->lib);
			fx->lib = NULL;
			return -1;
		}
		entry = SK_PTR_TO_FN(shell_plugin_entry_fn, raw);
		(void)entry(fx->boot.context, fx->boot.api);
	}
	fx->ui = (const sk_ui_api_t*)fx->boot.api->get_api(fx->boot.context, SK_UI_API_TYPE_ID);
	return fx->ui != NULL ? 0 : -1;
}

static void shell_ui_fixture_stop(shell_ui_fixture_t* fx) {
	const sk_platform_api_t* plat = NULL;
	if (fx->boot.context != NULL) {
		plat = fx->boot.api->platform_api(fx->boot.context);
		sk_app_shutdown(fx->boot.context);
	}
	if (fx->lib != NULL && plat != NULL) {
		plat->lib_close(fx->lib);
	}
	memset(fx, 0, sizeof(*fx));
}

/* Boot the editor registries + shell on a ui fixture. */
static sk_editor_shell_t* shell_fixture_boot(shell_ui_fixture_t* fx, const sk_editor_api_t** out_editor) {
	sk_editor_shell_t* shell;
	sk_editor_bind_tables(fx->boot.context, fx->boot.api);
	sk_editor_workspace_register_impls(fx->boot.context, fx->boot.api);
	/* Register the Project Browser + Console + Entity Tree before the
	 * main-window scaffolds so their real impls win window_open
	 * (docs/editor/window-table-pattern.md). */
	sk_editor_project_browser_register(fx->boot.context, fx->boot.api);
	sk_editor_console_register(fx->boot.context, fx->boot.api);
	sk_editor_entity_tree_register(fx->boot.context, fx->boot.api);
	sk_editor_windows_register_impls(fx->boot.context, fx->boot.api);
	/* Isolate persist from AppFolder leftovers: point at a missing temp file
	 * so shell_create's layout_init does not pick up another test's document. */
	{
		const sk_filesystem_api_t* fs = fx->boot.api->filesystem_api(fx->boot.context);
		char tmp[SK_FS_PATH_MAX];
		char path[SK_FS_PATH_MAX];
		if (fs != NULL && fs->temp_folder(tmp, (u32)sizeof(tmp)) == 0) {
			if (sk_path_join(sk_str_view_cstr(tmp), sk_str_view_cstr("skore-apx368-shell-layout.json"), path, (u32)sizeof(path)) >= 0) {
				if (fs->get_file_status(path) == SK_FILE_STATUS_FILE) {
					(void)fs->remove(path);
				}
				sk_editor_layout_set_path(fx->boot.context, fx->boot.api, path);
			}
		}
	}
	if (out_editor != NULL) {
		*out_editor = (const sk_editor_api_t*)fx->boot.api->get_api(fx->boot.context, SK_EDITOR_API_TYPE_ID);
	}
	shell = sk_editor_shell_create(fx->boot.context, fx->boot.api, fx->ui);
	return shell;
}

/* Open a menu popup, lay it out, then press+release at the center of a menu
 * item node and run a shell frame so the click action fires. */
static void shell_test_click_menu_item(sk_editor_shell_t* shell, const sk_ui_api_t* ui, sk_ui_node_t menu, sk_ui_node_t item) {
	sk_ui_rect_t rect;
	sk_ui_input_event_t ev;
	TEST_ASSERT_EQUAL_INT(0, ui->menu_set_open(shell->ctx, menu, 1));
	TEST_ASSERT_EQUAL_INT(0, sk_editor_shell_frame(shell, 1280.0f, 720.0f, 1.0f, 1.0f));
	TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(shell->ctx, item, &rect, NULL));
	TEST_ASSERT_TRUE(rect.width > 0.0f && rect.height > 0.0f);

	memset(&ev, 0, sizeof(ev));
	ev.kind = SK_UI_INPUT_POINTER_MOVE;
	ev.x = rect.x + rect.width * 0.5f;
	ev.y = rect.y + rect.height * 0.5f;
	(void)ui->input_dispatch(shell->ctx, &ev);
	memset(&ev, 0, sizeof(ev));
	ev.kind = SK_UI_INPUT_POINTER_BUTTON;
	ev.x = rect.x + rect.width * 0.5f;
	ev.y = rect.y + rect.height * 0.5f;
	ev.button = SK_UI_POINTER_BUTTON_LEFT;
	ev.down = 1;
	(void)ui->input_dispatch(shell->ctx, &ev);
	memset(&ev, 0, sizeof(ev));
	ev.kind = SK_UI_INPUT_POINTER_BUTTON;
	ev.x = rect.x + rect.width * 0.5f;
	ev.y = rect.y + rect.height * 0.5f;
	ev.button = SK_UI_POINTER_BUTTON_LEFT;
	ev.down = 0;
	(void)ui->input_dispatch(shell->ctx, &ev);
	TEST_ASSERT_EQUAL_INT(0, sk_editor_shell_frame(shell, 1280.0f, 720.0f, 1.0f, 1.0f));
}

SK_TEST(editor_shell_frame_menu_toolbar_dock) {
	shell_ui_fixture_t fx;
	sk_editor_shell_t* shell;
	const sk_ui_api_t* ui;
	sk_editor_workspace_t* scene_ws;
	sk_ui_context_t* dock_ctx;
	sk_ui_dock_node_t root;
	sk_ui_node_t host;
	const sk_ui_draw_list_t* dl;

	TEST_ASSERT_EQUAL_INT(0, shell_ui_fixture_start(&fx));
	shell = shell_fixture_boot(&fx, NULL);
	TEST_ASSERT_NOT_NULL(shell);
	ui = fx.ui;

	/* Frame chrome is live: menu bar, toolbar, dock host, workspace tabs. */
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(sk_editor_shell_menu_bar(shell)));
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(sk_editor_shell_toolbar(shell)));
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(sk_editor_shell_dock_host(shell)));
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(sk_editor_shell_find_menu(shell, "shell.ws.tabs")));

	/* Menu surface: top-level menus exist; window toggles exist; no graph
	 * node menus (manifest §4). */
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(sk_editor_shell_find_menu(shell, "shell.menu.file")));
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(sk_editor_shell_find_menu(shell, "shell.menu.edit")));
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(sk_editor_shell_find_menu(shell, "shell.menu.tools")));
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(sk_editor_shell_find_menu(shell, "shell.menu.build")));
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(sk_editor_shell_find_menu(shell, "shell.menu.window")));
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(sk_editor_shell_find_menu(shell, "shell.menu.help")));
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(sk_editor_shell_find_menu(shell, "shell.menu.window.project_browser")));
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(sk_editor_shell_find_menu(shell, "shell.menu.window.entity_tree")));
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(sk_editor_shell_find_menu(shell, "shell.menu.edit.packages")));
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(sk_editor_shell_find_menu(shell, "shell.menu.file.save_all")));
	TEST_ASSERT_FALSE(sk_ui_node_is_valid(sk_editor_shell_find_menu(shell, "shell.menu.window.graph_editor")));

	/* Toolbar buttons exist; undo/redo are inert (disabled). */
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(sk_editor_shell_find_menu(shell, "shell.toolbar.save_all")));
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(sk_editor_shell_find_menu(shell, "shell.toolbar.undo")));
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(sk_editor_shell_find_menu(shell, "shell.toolbar.play")));
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(sk_editor_shell_find_menu(shell, "shell.toolbar.reset_layout")));

	/* A frame runs the full pipeline and produces draw output. */
	TEST_ASSERT_EQUAL_INT(0, sk_editor_shell_frame(shell, 1280.0f, 720.0f, 1.0f, 1.0f));
	dl = ui->get_draw_list(shell->ctx);
	TEST_ASSERT_NOT_NULL(dl);
	TEST_ASSERT_TRUE(dl->command_count > 0u || dl->vertex_count > 0u);

	/* The default Scene workspace is live on the shell context and its dock
	 * host chrome is embedded under the shell's dock host view. */
	scene_ws = sk_editor_workspace_active(fx.boot.context, fx.boot.api);
	TEST_ASSERT_NOT_NULL(scene_ws);
	TEST_ASSERT_EQUAL_PTR(shell->ctx, sk_editor_workspace_dock_context(scene_ws));
	root = sk_editor_workspace_dock_root(scene_ws);
	TEST_ASSERT_TRUE(sk_ui_dock_node_is_valid(root));
	host = ui->dockspace_host_node(shell->ctx, root);
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(host));
	TEST_ASSERT_TRUE(sk_ui_node_eq(ui->node_parent(shell->ctx, host), sk_editor_shell_dock_host(shell)));

	/* Scene default windows are open (registry) and docked in the model. */
	dock_ctx = sk_editor_workspace_dock_context(scene_ws);
	TEST_ASSERT_NOT_NULL(sk_editor_window_by_type(fx.boot.context, fx.boot.api, SK_EDITOR_WINDOW_CONSOLE));
	TEST_ASSERT_NOT_NULL(sk_editor_window_by_type(fx.boot.context, fx.boot.api, SK_EDITOR_WINDOW_PROJECT_BROWSER));
	TEST_ASSERT_EQUAL_INT(1, ui->dock_window_is_docked(dock_ctx, "sk.editor_window.scene_view"));
	TEST_ASSERT_EQUAL_INT(1, ui->dock_window_is_docked(dock_ctx, "sk.editor_window.console"));

	sk_editor_shell_destroy(shell);
	shell_ui_fixture_stop(&fx);
}

SK_TEST(editor_shell_window_menu_uses_registry) {
	shell_ui_fixture_t fx;
	sk_editor_shell_t* shell;
	const sk_ui_api_t* ui;
	sk_ui_node_t window_menu;
	sk_ui_node_t item;
	sk_editor_window_t* window;

	TEST_ASSERT_EQUAL_INT(0, shell_ui_fixture_start(&fx));
	shell = shell_fixture_boot(&fx, NULL);
	TEST_ASSERT_NOT_NULL(shell);
	ui = fx.ui;
	window_menu = sk_editor_shell_find_menu(shell, "shell.menu.window");
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(window_menu));

	/* Window/Project Browser goes through the Project Browser ops table
	 * (registered with add_impl), never a direct symbol. */
	item = sk_editor_shell_find_menu(shell, "shell.menu.window.project_browser");
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(item));
	shell_test_click_menu_item(shell, ui, window_menu, item);
	window = sk_editor_window_by_type(fx.boot.context, fx.boot.api, SK_EDITOR_WINDOW_PROJECT_BROWSER);
	TEST_ASSERT_NOT_NULL(window);
	TEST_ASSERT_EQUAL_STRING("Project Browser", window->title);
	TEST_ASSERT_EQUAL_INT(1, ui->dock_window_is_docked(shell->ctx, "sk.editor_window.project_browser"));

	/* Window/History opens through the window impl registry. */
	item = sk_editor_shell_find_menu(shell, "shell.menu.window.history");
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(item));
	shell_test_click_menu_item(shell, ui, window_menu, item);
	window = sk_editor_window_by_type(fx.boot.context, fx.boot.api, SK_EDITOR_WINDOW_HISTORY);
	TEST_ASSERT_NOT_NULL(window);
	TEST_ASSERT_EQUAL_STRING("History", window->title);

	/* Re-clicking focuses instead of duplicating (one instance per type). */
	item = sk_editor_shell_find_menu(shell, "shell.menu.window.history");
	shell_test_click_menu_item(shell, ui, window_menu, item);
	TEST_ASSERT_EQUAL_PTR(window, sk_editor_window_by_type(fx.boot.context, fx.boot.api, SK_EDITOR_WINDOW_HISTORY));

	sk_editor_shell_destroy(shell);
	shell_ui_fixture_stop(&fx);
}

SK_TEST(editor_shell_window_close_and_toolbar) {
	shell_ui_fixture_t fx;
	sk_editor_shell_t* shell;
	const sk_ui_api_t* ui;
	sk_editor_window_t* window;
	sk_ui_rect_t rect;
	sk_ui_input_event_t ev;

	TEST_ASSERT_EQUAL_INT(0, shell_ui_fixture_start(&fx));
	shell = shell_fixture_boot(&fx, NULL);
	TEST_ASSERT_NOT_NULL(shell);
	ui = fx.ui;

	/* Close through the registry: opening the Scene defaults then closing
	 * the console removes the instance and its dock tab. */
	window = sk_editor_window_by_type(fx.boot.context, fx.boot.api, SK_EDITOR_WINDOW_CONSOLE);
	TEST_ASSERT_NOT_NULL(window);
	sk_editor_shell_focus_window(shell, window);
	sk_editor_shell_close_window(shell, window);
	TEST_ASSERT_NULL(sk_editor_window_by_type(fx.boot.context, fx.boot.api, SK_EDITOR_WINDOW_CONSOLE));
	TEST_ASSERT_EQUAL_INT(0, ui->dock_window_is_docked(shell->ctx, "sk.editor_window.console"));

	/* Toolbar: Save All is inert-but-clickable; Play toggles the mock sim. */
	TEST_ASSERT_EQUAL_INT(0, sk_editor_shell_sim_playing(shell));
	TEST_ASSERT_EQUAL_INT(0, sk_editor_shell_frame(shell, 1280.0f, 720.0f, 1.0f, 1.0f));
	{
		sk_ui_node_t play = sk_editor_shell_find_menu(shell, "shell.toolbar.play");
		TEST_ASSERT_TRUE(sk_ui_node_is_valid(play));
		TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(shell->ctx, play, &rect, NULL));
		memset(&ev, 0, sizeof(ev));
		ev.kind = SK_UI_INPUT_POINTER_MOVE;
		ev.x = rect.x + rect.width * 0.5f;
		ev.y = rect.y + rect.height * 0.5f;
		(void)ui->input_dispatch(shell->ctx, &ev);
		memset(&ev, 0, sizeof(ev));
		ev.kind = SK_UI_INPUT_POINTER_BUTTON;
		ev.x = rect.x + rect.width * 0.5f;
		ev.y = rect.y + rect.height * 0.5f;
		ev.button = SK_UI_POINTER_BUTTON_LEFT;
		ev.down = 1;
		(void)ui->input_dispatch(shell->ctx, &ev);
		memset(&ev, 0, sizeof(ev));
		ev.kind = SK_UI_INPUT_POINTER_BUTTON;
		ev.x = rect.x + rect.width * 0.5f;
		ev.y = rect.y + rect.height * 0.5f;
		ev.button = SK_UI_POINTER_BUTTON_LEFT;
		ev.down = 0;
		(void)ui->input_dispatch(shell->ctx, &ev);
	}
	TEST_ASSERT_EQUAL_INT(0, sk_editor_shell_frame(shell, 1280.0f, 720.0f, 1.0f, 1.0f));
	TEST_ASSERT_EQUAL_INT(1, sk_editor_shell_sim_playing(shell));

	/* Undo is an inert entry: clicking it changes nothing (sim stays on,
	 * no window opens through the registry). */
	{
		sk_ui_node_t undo = sk_editor_shell_find_menu(shell, "shell.toolbar.undo");
		sk_editor_window_t* packages;
		TEST_ASSERT_TRUE(sk_ui_node_is_valid(undo));
		TEST_ASSERT_EQUAL_INT(0, ui->node_get_abs_rect(shell->ctx, undo, &rect, NULL));
		memset(&ev, 0, sizeof(ev));
		ev.kind = SK_UI_INPUT_POINTER_MOVE;
		ev.x = rect.x + rect.width * 0.5f;
		ev.y = rect.y + rect.height * 0.5f;
		(void)ui->input_dispatch(shell->ctx, &ev);
		memset(&ev, 0, sizeof(ev));
		ev.kind = SK_UI_INPUT_POINTER_BUTTON;
		ev.x = rect.x + rect.width * 0.5f;
		ev.y = rect.y + rect.height * 0.5f;
		ev.button = SK_UI_POINTER_BUTTON_LEFT;
		ev.down = 1;
		(void)ui->input_dispatch(shell->ctx, &ev);
		memset(&ev, 0, sizeof(ev));
		ev.kind = SK_UI_INPUT_POINTER_BUTTON;
		ev.x = rect.x + rect.width * 0.5f;
		ev.y = rect.y + rect.height * 0.5f;
		ev.button = SK_UI_POINTER_BUTTON_LEFT;
		ev.down = 0;
		(void)ui->input_dispatch(shell->ctx, &ev);
		TEST_ASSERT_EQUAL_INT(0, sk_editor_shell_frame(shell, 1280.0f, 720.0f, 1.0f, 1.0f));
		TEST_ASSERT_EQUAL_INT(1, sk_editor_shell_sim_playing(shell));
		/* Packages is on-demand (mask 0): an inert Undo click must not open it. */
		packages = sk_editor_window_by_type(fx.boot.context, fx.boot.api, SK_EDITOR_WINDOW_PACKAGES);
		TEST_ASSERT_NULL(packages);
	}

	sk_editor_shell_destroy(shell);
	shell_ui_fixture_stop(&fx);
}

SK_TEST(editor_shell_workspace_switch_rebuilds_dock) {
	shell_ui_fixture_t fx;
	sk_editor_shell_t* shell;
	const sk_ui_api_t* ui;
	sk_editor_workspace_t* ws;
	sk_ui_context_t* dock_ctx;
	sk_ui_node_t host;
	sk_ui_dock_node_t root;

	TEST_ASSERT_EQUAL_INT(0, shell_ui_fixture_start(&fx));
	shell = shell_fixture_boot(&fx, NULL);
	TEST_ASSERT_NOT_NULL(shell);
	ui = fx.ui;

	/* Start on Scene; graph windows are not open yet. */
	TEST_ASSERT_NULL(sk_editor_window_by_type(fx.boot.context, fx.boot.api, SK_EDITOR_WINDOW_GRAPH_EDITOR));

	/* Switch to Graph: its dock model is built on the shared context, the
	 * Graph Editor window opens through the registry, and the scene model is
	 * torn down (only one live model at a time). */
	ws = sk_editor_shell_switch_workspace(shell, SK_EDITOR_WORKSPACE_GRAPH);
	TEST_ASSERT_NOT_NULL(ws);
	TEST_ASSERT_EQUAL_INT(0, sk_editor_shell_frame(shell, 1280.0f, 720.0f, 1.0f, 1.0f));
	TEST_ASSERT_EQUAL_PTR(ws, sk_editor_workspace_active(fx.boot.context, fx.boot.api));
	TEST_ASSERT_NOT_NULL(sk_editor_window_by_type(fx.boot.context, fx.boot.api, SK_EDITOR_WINDOW_GRAPH_EDITOR));
	TEST_ASSERT_NOT_NULL(sk_editor_window_by_type(fx.boot.context, fx.boot.api, SK_EDITOR_WINDOW_CONSOLE));
	dock_ctx = sk_editor_workspace_dock_context(ws);
	TEST_ASSERT_EQUAL_PTR(shell->ctx, dock_ctx);
	TEST_ASSERT_EQUAL_INT(1, ui->dock_window_is_docked(dock_ctx, "sk.editor_window.graph_editor"));
	root = sk_editor_workspace_dock_root(ws);
	TEST_ASSERT_TRUE(sk_ui_dock_node_is_valid(root));
	host = ui->dockspace_host_node(shell->ctx, root);
	TEST_ASSERT_TRUE(sk_ui_node_is_valid(host));
	TEST_ASSERT_TRUE(sk_ui_node_eq(ui->node_parent(shell->ctx, host), sk_editor_shell_dock_host(shell)));

	/* Switch back to Scene: the scene dock model is rebuilt on the shared
	 * context and hosts the scene defaults again. */
	ws = sk_editor_shell_switch_workspace(shell, SK_EDITOR_WORKSPACE_SCENE);
	TEST_ASSERT_NOT_NULL(ws);
	TEST_ASSERT_EQUAL_INT(0, sk_editor_shell_frame(shell, 1280.0f, 720.0f, 1.0f, 1.0f));
	dock_ctx = sk_editor_workspace_dock_context(ws);
	TEST_ASSERT_EQUAL_PTR(shell->ctx, dock_ctx);
	TEST_ASSERT_EQUAL_INT(1, ui->dock_window_is_docked(dock_ctx, "sk.editor_window.scene_view"));
	TEST_ASSERT_EQUAL_INT(1, ui->dock_window_is_docked(dock_ctx, "sk.editor_window.console"));

	sk_editor_shell_destroy(shell);
	shell_ui_fixture_stop(&fx);
}

#endif /* SK_TESTS */
