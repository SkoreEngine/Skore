/*
 * widget_review.c — named widget scenes + per-state PNG capture for
 * docs/widget-lavapipe-png-review.md. Agents build sk-sandbox, run under
 * lavapipe, and look at the PNGs. No CTest, no LLM CLI.
 */

#include "widget_review.h"

#include "path.h"

#include <stdio.h>
#include <string.h>

#define WIDGET_BTN_W 120.0f
#define WIDGET_BTN_ID "review-button"

typedef enum sandbox_widget_state_t {
	SANDBOX_WS_DEFAULT = 0,
	SANDBOX_WS_HOVERED,
	SANDBOX_WS_PRESSED,
	SANDBOX_WS_DISABLED,
	SANDBOX_WS_FOCUSED,
	SANDBOX_WS_COUNT
} sandbox_widget_state_t;

#define SANDBOX_WS_BIT_DEFAULT (1u << SANDBOX_WS_DEFAULT)
#define SANDBOX_WS_BIT_HOVERED (1u << SANDBOX_WS_HOVERED)
#define SANDBOX_WS_BIT_PRESSED (1u << SANDBOX_WS_PRESSED)
#define SANDBOX_WS_BIT_DISABLED (1u << SANDBOX_WS_DISABLED)
#define SANDBOX_WS_BIT_FOCUSED (1u << SANDBOX_WS_FOCUSED)
#define SANDBOX_WS_BITS_INTERACTIVE (SANDBOX_WS_BIT_DEFAULT | SANDBOX_WS_BIT_HOVERED | SANDBOX_WS_BIT_PRESSED | SANDBOX_WS_BIT_DISABLED | SANDBOX_WS_BIT_FOCUSED)

typedef i32 (*sandbox_widget_build_fn)(const sandbox_widget_host_t* host, sk_ui_node_t* out_target);

typedef struct sandbox_widget_desc_t {
	const_chr_t name;
	const_chr_t alias;
	const_chr_t manifest;
	u32 states;
	u32 width;
	u32 height;
	sandbox_widget_build_fn build;
} sandbox_widget_desc_t;

static const_chr_t sandbox_ws_name(sandbox_widget_state_t st) {
	switch (st) {
	case SANDBOX_WS_DEFAULT:
		return "default";
	case SANDBOX_WS_HOVERED:
		return "hovered";
	case SANDBOX_WS_PRESSED:
		return "pressed";
	case SANDBOX_WS_DISABLED:
		return "disabled";
	case SANDBOX_WS_FOCUSED:
		return "focused";
	case SANDBOX_WS_COUNT:
	default:
		return "unknown";
	}
}

static i32 sandbox_ws_parse(const_chr_t name, sandbox_widget_state_t* out) {
	i32 i;
	if (name == NULL || name[0] == '\0') {
		return -1;
	}
	for (i = 0; i < SANDBOX_WS_COUNT; ++i) {
		if (strcmp(name, sandbox_ws_name((sandbox_widget_state_t)i)) == 0) {
			*out = (sandbox_widget_state_t)i;
			return 0;
		}
	}
	return -1;
}

static i32 sandbox_name_eq(const_chr_t a, const_chr_t b) {
	u32 i;
	if (a == NULL || b == NULL) {
		return 0;
	}
	for (i = 0u;; ++i) {
		unsigned char ca = (unsigned char)a[i];
		unsigned char cb = (unsigned char)b[i];
		if (ca >= 'A' && ca <= 'Z') {
			ca = (unsigned char)(ca - 'A' + 'a');
		}
		if (cb >= 'A' && cb <= 'Z') {
			cb = (unsigned char)(cb - 'A' + 'a');
		}
		if (ca != cb) {
			return 0;
		}
		if (ca == '\0') {
			return 1;
		}
	}
}

static void sandbox_widget_style_stage(const sandbox_widget_host_t* host) {
	const sk_ui_api_t* ui = host->ui;
	sk_ui_node_t root = ui->context_root(host->ctx);
	sk_ui_style_props_t p;

	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_JUSTIFY_CONTENT | SK_UI_SP_ALIGN_ITEMS | SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_PADDING | SK_UI_SP_BACKGROUND_COLOR;
	p.layout.flex_direction = SK_UI_FLEX_COLUMN;
	p.layout.justify_content = SK_UI_JUSTIFY_CENTER;
	p.layout.align_items = SK_UI_ALIGN_CENTER;
	p.layout.width = sk_ui_pt((f32)host->width);
	p.layout.height = sk_ui_pt((f32)host->height);
	p.layout.padding.left = 24.0f;
	p.layout.padding.top = 24.0f;
	p.layout.padding.right = 24.0f;
	p.layout.padding.bottom = 24.0f;
	p.background_color = sk_ui_rgba(0.10f, 0.11f, 0.13f, 1.0f);
	(void)ui->node_set_inline_style(host->ctx, root, &p);
}

static i32 sandbox_widget_build_button(const sandbox_widget_host_t* host, sk_ui_node_t* out_target) {
	const sk_ui_api_t* ui = host->ui;
	sk_ui_node_t root = ui->context_root(host->ctx);
	sk_ui_node_t btn;

	sandbox_widget_style_stage(host);
	btn = ui->widget_button(host->ctx, root, "OK", WIDGET_BTN_ID);
	if (!sk_ui_node_is_valid(btn)) {
		fprintf(stderr, "sk-sandbox: widget_button failed\n");
		return -1;
	}
	/* Editor modal buttons use an explicit 120-wide size (WIDGET_MANIFEST §2). */
	(void)ui->button_set_size(host->ctx, btn, WIDGET_BTN_W, 0.0f);
	*out_target = btn;
	return 0;
}

static i32 sandbox_widget_build_small_button(const sandbox_widget_host_t* host, sk_ui_node_t* out_target) {
	const sk_ui_api_t* ui = host->ui;
	sk_ui_node_t root = ui->context_root(host->ctx);
	sk_ui_node_t btn;

	sandbox_widget_style_stage(host);
	/* PackagesWindow: SmallButton(ICON_FA_TRASH) — compact icon/text chrome. */
	btn = ui->widget_small_button(host->ctx, root, "Del", "review-small");
	if (!sk_ui_node_is_valid(btn)) {
		fprintf(stderr, "sk-sandbox: widget_small_button failed\n");
		return -1;
	}
	*out_target = btn;
	return 0;
}

static i32 sandbox_widget_build_invisible_button(const sandbox_widget_host_t* host, sk_ui_node_t* out_target) {
	const sk_ui_api_t* ui = host->ui;
	sk_ui_node_t root = ui->context_root(host->ctx);
	sk_ui_node_t btn;

	sandbox_widget_style_stage(host);
	btn = ui->widget_invisible_button(host->ctx, root, "review-invisible", 80.0f, 40.0f, SK_UI_BUTTON_FLAG_MOUSE_LEFT | SK_UI_BUTTON_FLAG_MOUSE_MIDDLE);
	if (!sk_ui_node_is_valid(btn)) {
		fprintf(stderr, "sk-sandbox: widget_invisible_button failed\n");
		return -1;
	}
	*out_target = btn;
	return 0;
}

static i32 sandbox_widget_build_selection_button(const sandbox_widget_host_t* host, sk_ui_node_t* out_target) {
	const sk_ui_api_t* ui = host->ui;
	sk_ui_node_t root = ui->context_root(host->ctx);
	sk_ui_node_t btn;

	sandbox_widget_style_stage(host);
	/* SceneView toolbar: ImGuiSelectionButton paints selected from a caller bool. */
	btn = ui->widget_selection_button(host->ctx, root, "Move", 1, "review-selection", 56.0f, 28.0f);
	if (!sk_ui_node_is_valid(btn)) {
		fprintf(stderr, "sk-sandbox: widget_selection_button failed\n");
		return -1;
	}
	*out_target = btn;
	return 0;
}

static i32 sandbox_widget_build_bordered_button(const sandbox_widget_host_t* host, sk_ui_node_t* out_target) {
	const sk_ui_api_t* ui = host->ui;
	sk_ui_node_t root = ui->context_root(host->ctx);
	sk_ui_node_t btn;

	sandbox_widget_style_stage(host);
	/* PropertiesWindow: ImGuiBorderedButton("Add Component", …). */
	btn = ui->widget_bordered_button(host->ctx, root, "Add Component", "review-bordered", 140.0f, 0.0f);
	if (!sk_ui_node_is_valid(btn)) {
		fprintf(stderr, "sk-sandbox: widget_bordered_button failed\n");
		return -1;
	}
	*out_target = btn;
	return 0;
}

static i32 sandbox_widget_build_arrow_button(const sandbox_widget_host_t* host, sk_ui_node_t* out_target) {
	const sk_ui_api_t* ui = host->ui;
	sk_ui_node_t root = ui->context_root(host->ctx);
	sk_ui_node_t btn;

	sandbox_widget_style_stage(host);
	btn = ui->widget_arrow_button(host->ctx, root, SK_UI_ARROW_RIGHT, "review-arrow");
	if (!sk_ui_node_is_valid(btn)) {
		fprintf(stderr, "sk-sandbox: widget_arrow_button failed\n");
		return -1;
	}
	*out_target = btn;
	return 0;
}

/* ---- text family (APX-340) ---- */

static i32 sandbox_widget_build_text(const sandbox_widget_host_t* host, sk_ui_node_t* out_target) {
	const sk_ui_api_t* ui = host->ui;
	sk_ui_node_t root = ui->context_root(host->ctx);
	sk_ui_node_t n;

	sandbox_widget_style_stage(host);
	/* ConsoleWindow status line: Text("%.2f ms (%.2f FPS)", …). */
	n = ui->widget_text(host->ctx, root, "16.67 ms (60.00 FPS)", "review-text");
	if (!sk_ui_node_is_valid(n)) {
		fprintf(stderr, "sk-sandbox: widget_text failed\n");
		return -1;
	}
	*out_target = n;
	return 0;
}

static i32 sandbox_widget_build_text_colored(const sandbox_widget_host_t* host, sk_ui_node_t* out_target) {
	const sk_ui_api_t* ui = host->ui;
	sk_ui_node_t root = ui->context_root(host->ctx);
	sk_ui_node_t n;

	sandbox_widget_style_stage(host);
	/* Editor.cpp save table: TextColored green "Created" / red "Deleted". */
	n = ui->widget_text_colored(host->ctx, root, "Created", sk_ui_rgba(0.10f, 0.80f, 0.10f, 1.0f), "review-text-colored");
	if (!sk_ui_node_is_valid(n)) {
		fprintf(stderr, "sk-sandbox: widget_text_colored failed\n");
		return -1;
	}
	*out_target = n;
	return 0;
}

static i32 sandbox_widget_build_text_disabled(const sandbox_widget_host_t* host, sk_ui_node_t* out_target) {
	const sk_ui_api_t* ui = host->ui;
	sk_ui_node_t root = ui->context_root(host->ctx);
	sk_ui_node_t n;

	sandbox_widget_style_stage(host);
	/* PackagesWindow: TextDisabled hint. */
	n = ui->widget_text_disabled(host->ctx, root, "No packages installed", "review-text-disabled");
	if (!sk_ui_node_is_valid(n)) {
		fprintf(stderr, "sk-sandbox: widget_text_disabled failed\n");
		return -1;
	}
	*out_target = n;
	return 0;
}

static i32 sandbox_widget_build_text_wrapped(const sandbox_widget_host_t* host, sk_ui_node_t* out_target) {
	const sk_ui_api_t* ui = host->ui;
	sk_ui_node_t root = ui->context_root(host->ctx);
	sk_ui_node_t n;
	sk_ui_style_props_t p;

	sandbox_widget_style_stage(host);
	/* TypeActions.cpp: TextWrapped animator-validation warning. */
	n = ui->widget_text_wrapped(host->ctx, root, "! Parent must be an AnimationControllerResource. Preview Entity on the controller must be provided.", "review-text-wrapped");
	if (!sk_ui_node_is_valid(n)) {
		fprintf(stderr, "sk-sandbox: widget_text_wrapped failed\n");
		return -1;
	}
	/* Narrow column so the wrap is visible (editor wraps at the pane width). */
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_MIN_WIDTH | SK_UI_SP_MAX_WIDTH;
	p.layout.width = sk_ui_pt(220.0f);
	p.layout.min_width = sk_ui_pt(220.0f);
	p.layout.max_width = sk_ui_pt(220.0f);
	(void)ui->node_merge_inline_style(host->ctx, n, &p);
	*out_target = n;
	return 0;
}

static i32 sandbox_widget_build_bullet_text(const sandbox_widget_host_t* host, sk_ui_node_t* out_target) {
	const sk_ui_api_t* ui = host->ui;
	sk_ui_node_t root = ui->context_root(host->ctx);
	sk_ui_node_t n;

	sandbox_widget_style_stage(host);
	/* BulletText (completeness; editor never calls it, manifest §22.3). */
	n = ui->widget_bullet_text(host->ctx, root, "A bulleted line of text", "review-bullet-text");
	if (!sk_ui_node_is_valid(n)) {
		fprintf(stderr, "sk-sandbox: widget_bullet_text failed\n");
		return -1;
	}
	*out_target = n;
	return 0;
}

static i32 sandbox_widget_build_separator_text(const sandbox_widget_host_t* host, sk_ui_node_t* out_target) {
	const sk_ui_api_t* ui = host->ui;
	sk_ui_node_t root = ui->context_root(host->ctx);
	sk_ui_node_t n;

	sandbox_widget_style_stage(host);
	/* ResourceDebuggerWindow: SeparatorText("Resource Info"). */
	n = ui->widget_separator_text(host->ctx, root, "Resource Info", "review-separator-text");
	if (!sk_ui_node_is_valid(n)) {
		fprintf(stderr, "sk-sandbox: widget_separator_text failed\n");
		return -1;
	}
	*out_target = n;
	return 0;
}

static const sandbox_widget_desc_t catalog[] = {
	{"button", NULL, "§2 Button", SANDBOX_WS_BITS_INTERACTIVE, 320u, 128u, sandbox_widget_build_button},
	{"small_button", "smallbutton", "§2 SmallButton", SANDBOX_WS_BITS_INTERACTIVE, 256u, 96u, sandbox_widget_build_small_button},
	{"invisible_button", "invisiblebutton", "§2 InvisibleButton", SANDBOX_WS_BITS_INTERACTIVE, 256u, 96u, sandbox_widget_build_invisible_button},
	{"selection_button", "selectionbutton", "§2 ImGuiSelectionButton", SANDBOX_WS_BITS_INTERACTIVE, 256u, 96u, sandbox_widget_build_selection_button},
	{"bordered_button", "borderedbutton", "§2 ImGuiBorderedButton", SANDBOX_WS_BITS_INTERACTIVE, 360u, 96u, sandbox_widget_build_bordered_button},
	{"arrow_button", "arrowbutton", "§2 ArrowButton", SANDBOX_WS_BITS_INTERACTIVE, 192u, 96u, sandbox_widget_build_arrow_button},
	{"text", "label", "§3 Text", SANDBOX_WS_BIT_DEFAULT | SANDBOX_WS_BIT_DISABLED, 320u, 96u, sandbox_widget_build_text},
	{"text_colored", "colored", "§3 TextColored", SANDBOX_WS_BIT_DEFAULT, 320u, 96u, sandbox_widget_build_text_colored},
	{"text_disabled", "textdisabled", "§3 TextDisabled", SANDBOX_WS_BIT_DEFAULT, 320u, 96u, sandbox_widget_build_text_disabled},
	{"text_wrapped", "wrapped", "§3 TextWrapped", SANDBOX_WS_BIT_DEFAULT, 320u, 160u, sandbox_widget_build_text_wrapped},
	{"bullet_text", "bullet", "§3 BulletText", SANDBOX_WS_BIT_DEFAULT, 320u, 96u, sandbox_widget_build_bullet_text},
	{"separator_text", "separatortext", "§3 SeparatorText", SANDBOX_WS_BIT_DEFAULT, 360u, 96u, sandbox_widget_build_separator_text},
	{"checkbox", NULL, "§4 Checkbox", SANDBOX_WS_BITS_INTERACTIVE, 256u, 128u, NULL},
	{"text_input", "input", "§5 InputText", SANDBOX_WS_BITS_INTERACTIVE, 384u, 96u, NULL},
	{"slider", NULL, "§6 Slider / Drag", SANDBOX_WS_BITS_INTERACTIVE, 384u, 96u, NULL},
	{"combo", "listbox", "§7 Combo / ListBox", SANDBOX_WS_BIT_DEFAULT | SANDBOX_WS_BIT_HOVERED | SANDBOX_WS_BIT_DISABLED | SANDBOX_WS_BIT_FOCUSED, 320u, 160u, NULL},
	{"tree", NULL, "§8 TreeNode / CollapsingHeader", SANDBOX_WS_BIT_DEFAULT | SANDBOX_WS_BIT_HOVERED | SANDBOX_WS_BIT_DISABLED | SANDBOX_WS_BIT_FOCUSED, 384u, 256u, NULL},
	{"table", NULL, "§9 Table", SANDBOX_WS_BIT_DEFAULT, 480u, 256u, NULL},
	{"tab_bar", "tab", "§10 TabBar", SANDBOX_WS_BIT_DEFAULT | SANDBOX_WS_BIT_HOVERED | SANDBOX_WS_BIT_FOCUSED, 384u, 96u, NULL},
	{"menu", "menubar", "§11 MenuBar / Menu / MenuItem", SANDBOX_WS_BIT_DEFAULT | SANDBOX_WS_BIT_HOVERED | SANDBOX_WS_BIT_DISABLED | SANDBOX_WS_BIT_FOCUSED, 384u, 160u, NULL},
	{"popup", "modal", "§12 Popup / Modal", SANDBOX_WS_BIT_DEFAULT, 384u, 192u, NULL},
	{"window", "layout", "§13 Child / Window / Layout", SANDBOX_WS_BIT_DEFAULT, 480u, 280u, NULL},
	{"separator", NULL, "§19 Separator / Spacing / SameLine", SANDBOX_WS_BIT_DEFAULT, 320u, 96u, NULL},
	{"color", NULL, "§15 ColorEdit / ColorPicker", SANDBOX_WS_BIT_DEFAULT | SANDBOX_WS_BIT_HOVERED | SANDBOX_WS_BIT_FOCUSED, 320u, 192u, NULL},
	{"image", NULL, "§16 Image / content item", SANDBOX_WS_BIT_DEFAULT, 256u, 192u, NULL},
	{"selectable", NULL, "§18 Selectable", SANDBOX_WS_BITS_INTERACTIVE, 320u, 128u, NULL},
	{"tooltip", NULL, "§20 Tooltip", SANDBOX_WS_BIT_DEFAULT, 320u, 128u, NULL},
};

static const sandbox_widget_desc_t* sandbox_widget_find(const_chr_t name) {
	u32 i;
	if (name == NULL || name[0] == '\0') {
		return NULL;
	}
	for (i = 0u; i < (u32)(sizeof(catalog) / sizeof(catalog[0])); ++i) {
		if (sandbox_name_eq(name, catalog[i].name) != 0) {
			return &catalog[i];
		}
		if (catalog[i].alias != NULL && sandbox_name_eq(name, catalog[i].alias) != 0) {
			return &catalog[i];
		}
	}
	return NULL;
}

void sandbox_widget_list(void) {
	u32 i;
	printf("%-12s %-52s %-6s %s\n", "widget", "states", "scene", "manifest");
	for (i = 0u; i < (u32)(sizeof(catalog) / sizeof(catalog[0])); ++i) {
		char states[96];
		i32 n;
		i32 st;
		i32 first = 1;
		n = 0;
		states[0] = '\0';
		for (st = 0; st < SANDBOX_WS_COUNT; ++st) {
			const_chr_t nm;
			i32 add;
			if ((catalog[i].states & (1u << (u32)st)) == 0u) {
				continue;
			}
			nm = sandbox_ws_name((sandbox_widget_state_t)st);
			add = snprintf(states + n, (size_t)((i32)sizeof(states) - n), "%s%s", first != 0 ? "" : ",", nm);
			if (add < 0 || n + add >= (i32)sizeof(states)) {
				break;
			}
			n += add;
			first = 0;
		}
		printf("%-12s %-52s %-6s %s\n", catalog[i].name, states, catalog[i].build != NULL ? "ready" : "none", catalog[i].manifest);
	}
	printf("aliases: label=text input=text_input listbox=combo tab=tab_bar menubar=menu modal=popup layout=window\n");
	printf("         smallbutton=small_button invisiblebutton=invisible_button selectionbutton=selection_button\n");
	printf("         borderedbutton=bordered_button arrowbutton=arrow_button\n");
	printf("         colored=text_colored textdisabled=text_disabled wrapped=text_wrapped bullet=bullet_text\n");
	printf("         separatortext=separator_text\n");
}

i32 sandbox_widget_lookup(const_chr_t name, u32* out_width, u32* out_height, i32* out_ready) {
	const sandbox_widget_desc_t* d = sandbox_widget_find(name);
	if (d == NULL) {
		return -1;
	}
	if (out_width != NULL) {
		*out_width = d->width;
	}
	if (out_height != NULL) {
		*out_height = d->height;
	}
	if (out_ready != NULL) {
		*out_ready = d->build != NULL ? 1 : 0;
	}
	return 0;
}

static i32 sandbox_widget_layout(const sandbox_widget_host_t* host) {
	const sk_ui_api_t* ui = host->ui;
	if (ui->style_resolve(host->ctx) != 0) {
		return -1;
	}
	if (ui->layout(host->ctx, (f32)host->width, (f32)host->height) != 0) {
		return -1;
	}
	return ui->layout_apply_scale(host->ctx, 1.0f, 1.0f);
}

static i32 sandbox_widget_apply_state(const sandbox_widget_host_t* host, sk_ui_node_t node, sandbox_widget_state_t st) {
	const sk_ui_api_t* ui = host->ui;
	(void)ui->focus_set(host->ctx, SK_UI_NODE_INVALID);
	if (ui->node_set_state(host->ctx, node, (u32)SK_UI_STATE_NONE) != 0) {
		return -1;
	}
	switch (st) {
	case SANDBOX_WS_DEFAULT:
		return 0;
	case SANDBOX_WS_HOVERED:
		return ui->node_set_state(host->ctx, node, (u32)SK_UI_STATE_HOVER);
	case SANDBOX_WS_PRESSED:
		return ui->node_set_state(host->ctx, node, (u32)SK_UI_STATE_ACTIVE);
	case SANDBOX_WS_DISABLED:
		return ui->node_set_state(host->ctx, node, (u32)SK_UI_STATE_DISABLED);
	case SANDBOX_WS_FOCUSED:
		if (ui->focus_set(host->ctx, node) == 0) {
			return 0;
		}
		return ui->node_set_state(host->ctx, node, (u32)SK_UI_STATE_FOCUSED);
	case SANDBOX_WS_COUNT:
	default:
		return -1;
	}
}

static i32 sandbox_widget_write_png(const sandbox_widget_host_t* host, const_chr_t png_path) {
	const sk_ui_api_t* ui = host->ui;
	sk_ui_paint_params_t paint;
	sk_ui_capture_frame_info_t finfo;
	sk_ui_cpu_image_t img;
	const sk_ui_draw_list_t* dl;
	sk_ui_draw_list_t empty;

	memset(&paint, 0, sizeof(paint));
	paint.font_system = host->fonts;
	paint.font = host->font;
	if (ui->paint(host->ctx, &paint) != 0) {
		fprintf(stderr, "sk-sandbox: paint failed\n");
		return -1;
	}

	dl = ui->get_draw_list(host->ctx);
	memset(&finfo, 0, sizeof(finfo));
	if (dl != NULL) {
		finfo.draw_list = dl;
	} else {
		memset(&empty, 0, sizeof(empty));
		finfo.draw_list = &empty;
	}
	finfo.font_system = host->fonts;
	finfo.font = host->font;
	memset(&img, 0, sizeof(img));
	if (ui->capture_frame(host->capture, &finfo, &img) != 0) {
		fprintf(stderr, "sk-sandbox: capture_frame failed\n");
		return -1;
	}
	if (ui->cpu_image_write_png(&img, host->fs, png_path) != 0) {
		fprintf(stderr, "sk-sandbox: cpu_image_write_png failed for '%s'\n", png_path);
		return -1;
	}
	printf("sk-sandbox: wrote %ux%u '%s'\n", img.width, img.height, png_path);
	return 0;
}

i32 sandbox_widget_run(const sandbox_widget_host_t* host, const_chr_t name, const_chr_t state_filter, const_chr_t out_dir) {
	const sandbox_widget_desc_t* d;
	sk_ui_node_t target;
	sandbox_widget_state_t only = SANDBOX_WS_DEFAULT;
	i32 filter_one = 0;
	i32 i;
	i32 wrote = 0;

	d = sandbox_widget_find(name);
	if (d == NULL) {
		fprintf(stderr, "sk-sandbox: unknown --widget '%s' (use --list)\n", name != NULL ? name : "");
		return -1;
	}
	if (d->build == NULL) {
		fprintf(stderr, "sk-sandbox: widget '%s' (%s) has no scene yet; add a builder in sandbox/widget_review.c\n", d->name, d->manifest);
		return -1;
	}
	if (state_filter != NULL && state_filter[0] != '\0' && strcmp(state_filter, "all") != 0) {
		if (sandbox_ws_parse(state_filter, &only) != 0) {
			fprintf(stderr, "sk-sandbox: unknown --state '%s' (default|hovered|pressed|disabled|focused|all)\n", state_filter);
			return -1;
		}
		if ((d->states & (1u << (u32)only)) == 0u) {
			fprintf(stderr, "sk-sandbox: widget '%s' has no '%s' state\n", d->name, sandbox_ws_name(only));
			return -1;
		}
		filter_one = 1;
	}

	target = SK_UI_NODE_INVALID;
	if (d->build(host, &target) != 0 || !sk_ui_node_is_valid(target)) {
		return -1;
	}
	if (sandbox_widget_layout(host) != 0) {
		fprintf(stderr, "sk-sandbox: widget layout failed\n");
		return -1;
	}

	for (i = 0; i < SANDBOX_WS_COUNT; ++i) {
		char file[64];
		char path[SK_FS_PATH_MAX];
		sandbox_widget_state_t st = (sandbox_widget_state_t)i;
		if ((d->states & (1u << (u32)i)) == 0u) {
			continue;
		}
		if (filter_one != 0 && st != only) {
			continue;
		}
		if (sandbox_widget_apply_state(host, target, st) != 0) {
			fprintf(stderr, "sk-sandbox: failed to apply state '%s'\n", sandbox_ws_name(st));
			return -1;
		}
		if (sandbox_widget_layout(host) != 0) {
			fprintf(stderr, "sk-sandbox: layout after state '%s' failed\n", sandbox_ws_name(st));
			return -1;
		}
		if (snprintf(file, sizeof(file), "%s_%s.png", d->name, sandbox_ws_name(st)) < 0) {
			return -1;
		}
		if (sk_path_join(sk_str_view_cstr(out_dir), sk_str_view_cstr(file), path, (u32)sizeof(path)) < 0) {
			fprintf(stderr, "sk-sandbox: cannot join output path\n");
			return -1;
		}
		if (sandbox_widget_write_png(host, path) != 0) {
			return -1;
		}
		++wrote;
	}
	if (wrote == 0) {
		fprintf(stderr, "sk-sandbox: no frames written for widget '%s'\n", d->name);
		return -1;
	}
	return 0;
}
