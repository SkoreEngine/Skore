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
	SANDBOX_WS_CHECKED,
	SANDBOX_WS_MIXED,
	SANDBOX_WS_MIN,
	SANDBOX_WS_MID,
	SANDBOX_WS_MAX,
	SANDBOX_WS_DRAGGING,
	SANDBOX_WS_TEXT_ENTRY,
	SANDBOX_WS_COUNT
} sandbox_widget_state_t;

#define SANDBOX_WS_BIT_DEFAULT (1u << SANDBOX_WS_DEFAULT)
#define SANDBOX_WS_BIT_HOVERED (1u << SANDBOX_WS_HOVERED)
#define SANDBOX_WS_BIT_PRESSED (1u << SANDBOX_WS_PRESSED)
#define SANDBOX_WS_BIT_DISABLED (1u << SANDBOX_WS_DISABLED)
#define SANDBOX_WS_BIT_FOCUSED (1u << SANDBOX_WS_FOCUSED)
#define SANDBOX_WS_BIT_CHECKED (1u << SANDBOX_WS_CHECKED)
#define SANDBOX_WS_BIT_MIXED (1u << SANDBOX_WS_MIXED)
#define SANDBOX_WS_BITS_INTERACTIVE (SANDBOX_WS_BIT_DEFAULT | SANDBOX_WS_BIT_HOVERED | SANDBOX_WS_BIT_PRESSED | SANDBOX_WS_BIT_DISABLED | SANDBOX_WS_BIT_FOCUSED)
#define SANDBOX_WS_BITS_CHECKBOX (SANDBOX_WS_BIT_DEFAULT | SANDBOX_WS_BIT_HOVERED | SANDBOX_WS_BIT_DISABLED | SANDBOX_WS_BIT_CHECKED | SANDBOX_WS_BIT_MIXED)
#define SANDBOX_WS_BITS_RADIO (SANDBOX_WS_BIT_DEFAULT | SANDBOX_WS_BIT_HOVERED | SANDBOX_WS_BIT_DISABLED | SANDBOX_WS_BIT_CHECKED)
#define SANDBOX_WS_BIT_MIN (1u << SANDBOX_WS_MIN)
#define SANDBOX_WS_BIT_MID (1u << SANDBOX_WS_MID)
#define SANDBOX_WS_BIT_MAX (1u << SANDBOX_WS_MAX)
#define SANDBOX_WS_BIT_DRAGGING (1u << SANDBOX_WS_DRAGGING)
#define SANDBOX_WS_BIT_TEXT_ENTRY (1u << SANDBOX_WS_TEXT_ENTRY)
#define SANDBOX_WS_BITS_SLIDER (SANDBOX_WS_BIT_MIN | SANDBOX_WS_BIT_MID | SANDBOX_WS_BIT_MAX | SANDBOX_WS_BIT_DRAGGING | SANDBOX_WS_BIT_TEXT_ENTRY | SANDBOX_WS_BIT_DISABLED)

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
	case SANDBOX_WS_CHECKED:
		return "checked";
	case SANDBOX_WS_MIXED:
		return "mixed";
	case SANDBOX_WS_MIN:
		return "min";
	case SANDBOX_WS_MID:
		return "mid";
	case SANDBOX_WS_MAX:
		return "max";
	case SANDBOX_WS_DRAGGING:
		return "dragging";
	case SANDBOX_WS_TEXT_ENTRY:
		return "text_entry";
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

/* ---- checkbox / radio (APX-341) ---- */

static i32 sandbox_widget_build_checkbox(const sandbox_widget_host_t* host, sk_ui_node_t* out_target) {
	const sk_ui_api_t* ui = host->ui;
	sk_ui_node_t root = ui->context_root(host->ctx);
	sk_ui_node_t n;

	sandbox_widget_style_stage(host);
	/* ConsoleWindow: Checkbox("Trace", &v) — box + label on the same item. */
	n = ui->widget_checkbox(host->ctx, root, 0, "review-checkbox");
	if (!sk_ui_node_is_valid(n)) {
		fprintf(stderr, "sk-sandbox: widget_checkbox failed\n");
		return -1;
	}
	(void)ui->checkbox_set_label(host->ctx, n, "Trace");
	*out_target = n;
	return 0;
}

static i32 sandbox_widget_build_radio(const sandbox_widget_host_t* host, sk_ui_node_t* out_target) {
	const sk_ui_api_t* ui = host->ui;
	sk_ui_node_t root = ui->context_root(host->ctx);
	sk_ui_node_t n;

	sandbox_widget_style_stage(host);
	n = ui->widget_radio(host->ctx, root, 0, "review-radio");
	if (!sk_ui_node_is_valid(n)) {
		fprintf(stderr, "sk-sandbox: widget_radio failed\n");
		return -1;
	}
	(void)ui->radio_set_label(host->ctx, n, "Smooth Camera");
	*out_target = n;
	return 0;
}

static i32 sandbox_widget_build_radio_group(const sandbox_widget_host_t* host, sk_ui_node_t* out_target) {
	const sk_ui_api_t* ui = host->ui;
	sk_ui_node_t root = ui->context_root(host->ctx);
	sk_ui_node_t group;
	sk_ui_node_t a;
	sk_ui_node_t b;
	sk_ui_node_t c;
	sk_ui_style_props_t p;

	sandbox_widget_style_stage(host);
	group = ui->widget_view(host->ctx, root, "review-radio-group");
	if (!sk_ui_node_is_valid(group)) {
		fprintf(stderr, "sk-sandbox: radio group view failed\n");
		return -1;
	}
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_ALIGN_ITEMS | SK_UI_SP_ROW_GAP;
	p.layout.flex_direction = SK_UI_FLEX_COLUMN;
	p.layout.align_items = SK_UI_ALIGN_FLEX_START;
	p.layout.row_gap = 8.0f;
	(void)ui->node_merge_inline_style(host->ctx, group, &p);
	a = ui->widget_radio(host->ctx, group, 1, "review-radio-a");
	b = ui->widget_radio(host->ctx, group, 0, "review-radio-b");
	c = ui->widget_radio(host->ctx, group, 0, "review-radio-c");
	if (!sk_ui_node_is_valid(a) || !sk_ui_node_is_valid(b) || !sk_ui_node_is_valid(c)) {
		fprintf(stderr, "sk-sandbox: widget_radio (group) failed\n");
		return -1;
	}
	(void)ui->radio_set_label(host->ctx, a, "Move");
	(void)ui->radio_set_label(host->ctx, b, "Rotate");
	(void)ui->radio_set_label(host->ctx, c, "Scale");
	*out_target = group;
	return 0;
}

static void sandbox_ti_size(const sandbox_widget_host_t* host, sk_ui_node_t n, f32 w, f32 h) {
	(void)host->ui->text_input_set_size(host->ctx, n, w, h);
}

static i32 sandbox_widget_build_text_input(const sandbox_widget_host_t* host, sk_ui_node_t* out_target) {
	const sk_ui_api_t* ui = host->ui;
	sk_ui_node_t root = ui->context_root(host->ctx);
	sk_ui_node_t n;

	sandbox_widget_style_stage(host);
	/* EntityTree inline rename / string property: filled single-line field. */
	n = ui->widget_text_input(host->ctx, root, "Entity_01", "review-text-input");
	if (!sk_ui_node_is_valid(n)) {
		fprintf(stderr, "sk-sandbox: widget_text_input failed\n");
		return -1;
	}
	sandbox_ti_size(host, n, 240.0f, 28.0f);
	*out_target = n;
	return 0;
}

static i32 sandbox_widget_build_text_input_hint(const sandbox_widget_host_t* host, sk_ui_node_t* out_target) {
	const sk_ui_api_t* ui = host->ui;
	sk_ui_node_t root = ui->context_root(host->ctx);
	sk_ui_node_t n;

	sandbox_widget_style_stage(host);
	/* ImGuiSearchInputText: magnifier + "Search" placeholder when empty. */
	n = ui->widget_search_input(host->ctx, root, "", "review-text-input-hint");
	if (!sk_ui_node_is_valid(n)) {
		fprintf(stderr, "sk-sandbox: widget_search_input failed\n");
		return -1;
	}
	sandbox_ti_size(host, n, 240.0f, 28.0f);
	*out_target = n;
	return 0;
}

static i32 sandbox_widget_build_text_input_selection(const sandbox_widget_host_t* host, sk_ui_node_t* out_target) {
	const sk_ui_api_t* ui = host->ui;
	sk_ui_node_t root = ui->context_root(host->ctx);
	sk_ui_node_t n;

	sandbox_widget_style_stage(host);
	n = ui->widget_text_input(host->ctx, root, "Hello World", "review-text-input-sel");
	if (!sk_ui_node_is_valid(n)) {
		fprintf(stderr, "sk-sandbox: widget_text_input (selection) failed\n");
		return -1;
	}
	sandbox_ti_size(host, n, 240.0f, 28.0f);
	(void)ui->focus_set(host->ctx, n);
	(void)ui->text_input_set_selection(host->ctx, n, 0, 5);
	*out_target = n;
	return 0;
}

static i32 sandbox_widget_build_text_input_multiline(const sandbox_widget_host_t* host, sk_ui_node_t* out_target) {
	const sk_ui_api_t* ui = host->ui;
	sk_ui_node_t root = ui->context_root(host->ctx);
	sk_ui_node_t n;
	const_chr_t src = "// generated HLSL\n"
					  "float4 main(float2 uv : TEXCOORD0) : SV_Target {\n"
					  "    float3 n = float3(0, 1, 0);\n"
					  "    float ndotl = saturate(dot(n, float3(0.4, 0.8, 0.2)));\n"
					  "    return float4(ndotl, ndotl, ndotl, 1);\n"
					  "    // extra lines force a vertical scrollbar\n"
					  "    // line 7\n"
					  "    // line 8\n"
					  "    // line 9\n"
					  "    // line 10\n"
					  "}";

	sandbox_widget_style_stage(host);
	/* Properties / shader log: InputTextMultiline with overflow scroll. */
	n = ui->widget_text_input_multiline(host->ctx, root, src, 320.0f, 120.0f, "review-text-input-ml");
	if (!sk_ui_node_is_valid(n)) {
		fprintf(stderr, "sk-sandbox: widget_text_input_multiline failed\n");
		return -1;
	}
	(void)ui->node_set_prop_f32(host->ctx, n, "scroll_y", 36.0f);
	*out_target = n;
	return 0;
}

static i32 sandbox_widget_build_text_input_readonly(const sandbox_widget_host_t* host, sk_ui_node_t* out_target) {
	const sk_ui_api_t* ui = host->ui;
	sk_ui_node_t root = ui->context_root(host->ctx);
	sk_ui_node_t n;

	sandbox_widget_style_stage(host);
	/* ImGuiInputTextReadOnly: UUID / path, still focusable. */
	n = ui->widget_text_input_readonly(host->ctx, root, "a1b2c3d4-e5f6-7890-abcd-ef1234567890", "review-text-input-ro");
	if (!sk_ui_node_is_valid(n)) {
		fprintf(stderr, "sk-sandbox: widget_text_input_readonly failed\n");
		return -1;
	}
	sandbox_ti_size(host, n, 320.0f, 28.0f);
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

static void sandbox_slider_size(const sandbox_widget_host_t* host, sk_ui_node_t n, f32 w, f32 h) {
	const sk_ui_api_t* ui = host->ui;
	sk_ui_style_props_t p;
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_MIN_WIDTH | SK_UI_SP_MIN_HEIGHT;
	p.layout.width = sk_ui_pt(w);
	p.layout.height = sk_ui_pt(h);
	p.layout.min_width = sk_ui_pt(w);
	p.layout.min_height = sk_ui_pt(h);
	(void)ui->node_merge_inline_style(host->ctx, n, &p);
}

static i32 sandbox_widget_build_slider(const sandbox_widget_host_t* host, sk_ui_node_t* out_target) {
	const sk_ui_api_t* ui = host->ui;
	sk_ui_node_t root = ui->context_root(host->ctx);
	sk_ui_node_t n;

	sandbox_widget_style_stage(host);
	/* SceneView FOV: SliderFloat("##fov", &cameraFov, 4, 120, "%.0f"). */
	n = ui->widget_slider(host->ctx, root, 4.0f, 120.0f, 62.0f, "review-slider");
	if (!sk_ui_node_is_valid(n)) {
		fprintf(stderr, "sk-sandbox: widget_slider failed\n");
		return -1;
	}
	(void)ui->slider_set_format(host->ctx, n, "%.0f");
	sandbox_slider_size(host, n, 280.0f, 22.0f);
	*out_target = n;
	return 0;
}

static i32 sandbox_widget_build_slider_int(const sandbox_widget_host_t* host, sk_ui_node_t* out_target) {
	const sk_ui_api_t* ui = host->ui;
	sk_ui_node_t root = ui->context_root(host->ctx);
	sk_ui_node_t n;

	sandbox_widget_style_stage(host);
	/* SceneView forced LOD: SliderInt format "LOD %d". */
	n = ui->widget_slider_int(host->ctx, root, 0, 8, 3, "LOD %d", "review-slider-int");
	if (!sk_ui_node_is_valid(n)) {
		fprintf(stderr, "sk-sandbox: widget_slider_int failed\n");
		return -1;
	}
	sandbox_slider_size(host, n, 280.0f, 22.0f);
	*out_target = n;
	return 0;
}

static i32 sandbox_widget_build_slider_float3(const sandbox_widget_host_t* host, sk_ui_node_t* out_target) {
	const sk_ui_api_t* ui = host->ui;
	sk_ui_node_t root = ui->context_root(host->ctx);
	sk_ui_node_t n;
	f32 v[3] = {0.25f, 0.50f, 0.75f};

	sandbox_widget_style_stage(host);
	n = ui->widget_slider_float_n(host->ctx, root, 3, 0.0f, 1.0f, v, "%.2f", "review-slider-f3");
	if (!sk_ui_node_is_valid(n)) {
		fprintf(stderr, "sk-sandbox: widget_slider_float_n failed\n");
		return -1;
	}
	sandbox_slider_size(host, n, 360.0f, 22.0f);
	*out_target = n;
	return 0;
}

static i32 sandbox_widget_build_drag_float(const sandbox_widget_host_t* host, sk_ui_node_t* out_target) {
	const sk_ui_api_t* ui = host->ui;
	sk_ui_node_t root = ui->context_root(host->ctx);
	sk_ui_node_t n;

	sandbox_widget_style_stage(host);
	/* PropertiesWindow material scalar: DragFloat("##v", &scalar, 0.01f). */
	n = ui->widget_drag_float(host->ctx, root, 0.01f, 0.0f, 1.0f, 0.35f, "%.3f", "review-drag");
	if (!sk_ui_node_is_valid(n)) {
		fprintf(stderr, "sk-sandbox: widget_drag_float failed\n");
		return -1;
	}
	sandbox_slider_size(host, n, 280.0f, 22.0f);
	*out_target = n;
	return 0;
}

static i32 sandbox_widget_build_drag_int(const sandbox_widget_host_t* host, sk_ui_node_t* out_target) {
	const sk_ui_api_t* ui = host->ui;
	sk_ui_node_t root = ui->context_root(host->ctx);
	sk_ui_node_t n;

	sandbox_widget_style_stage(host);
	n = ui->widget_drag_int(host->ctx, root, 1.0f, 0, 100, 40, "%d", "review-drag-int");
	if (!sk_ui_node_is_valid(n)) {
		fprintf(stderr, "sk-sandbox: widget_drag_int failed\n");
		return -1;
	}
	sandbox_slider_size(host, n, 280.0f, 22.0f);
	*out_target = n;
	return 0;
}

static void sandbox_widget_style_fill(const sandbox_widget_host_t* host) {
	const sk_ui_api_t* ui = host->ui;
	sk_ui_node_t root = ui->context_root(host->ctx);
	sk_ui_style_props_t p;

	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_JUSTIFY_CONTENT | SK_UI_SP_ALIGN_ITEMS | SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_PADDING | SK_UI_SP_BACKGROUND_COLOR;
	p.layout.flex_direction = SK_UI_FLEX_COLUMN;
	p.layout.justify_content = SK_UI_JUSTIFY_FLEX_START;
	p.layout.align_items = SK_UI_ALIGN_STRETCH;
	p.layout.width = sk_ui_pt((f32)host->width);
	p.layout.height = sk_ui_pt((f32)host->height);
	p.layout.padding.left = 16.0f;
	p.layout.padding.top = 16.0f;
	p.layout.padding.right = 16.0f;
	p.layout.padding.bottom = 16.0f;
	p.background_color = sk_ui_rgba(0.10f, 0.11f, 0.13f, 1.0f);
	(void)ui->node_set_inline_style(host->ctx, root, &p);
}

static i32 sandbox_widget_build_window(const sandbox_widget_host_t* host, sk_ui_node_t* out_target) {
	const sk_ui_api_t* ui = host->ui;
	sk_ui_node_t root = ui->context_root(host->ctx);
	sk_ui_node_t win;
	sk_ui_node_t body;
	sk_ui_node_t child;
	sk_ui_node_t content;
	sk_ui_style_props_t p;
	u32 i;
	static i32 s_open = 1;

	sandbox_widget_style_fill(host);
	s_open = 1;
	win = ui->widget_window(host->ctx, root, "Console", "review-window", &s_open);
	if (!sk_ui_node_is_valid(win)) {
		fprintf(stderr, "sk-sandbox: widget_window failed\n");
		return -1;
	}
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_FLEX_GROW;
	p.layout.width = sk_ui_percent(100.0f);
	p.layout.height = sk_ui_pt((f32)host->height - 32.0f);
	p.layout.flex_grow = 1.0f;
	(void)ui->node_merge_inline_style(host->ctx, win, &p);

	body = ui->editor_window_content(host->ctx, win);
	child = ui->widget_child(host->ctx, body, "review-window-child", 0.0f, 0.0f, SK_UI_CHILD_FLAG_BORDER | SK_UI_CHILD_FLAG_HORIZONTAL_SCROLLBAR);
	content = ui->child_content(host->ctx, child);
	if (sk_ui_node_is_valid(content)) {
		for (i = 0u; i < 6u; ++i) {
			char line[48];
			(void)snprintf(line, sizeof(line), "[info] console line %u", i + 1u);
			(void)ui->widget_text(host->ctx, content, line, NULL);
		}
		(void)ui->scroll_view_set_content_size(host->ctx, child, 420.0f, 160.0f);
	}
	*out_target = win;
	return 0;
}

static i32 sandbox_widget_build_fullscreen(const sandbox_widget_host_t* host, sk_ui_node_t* out_target) {
	const sk_ui_api_t* ui = host->ui;
	sk_ui_node_t root = ui->context_root(host->ctx);
	sk_ui_node_t fs;
	sk_ui_node_t title;

	sandbox_widget_style_fill(host);
	fs = ui->widget_fullscreen(host->ctx, root, "review-fullscreen");
	if (!sk_ui_node_is_valid(fs)) {
		fprintf(stderr, "sk-sandbox: widget_fullscreen failed\n");
		return -1;
	}
	(void)ui->widget_spring(host->ctx, fs, 1.0f, "review-fs-top");
	{
		sk_ui_node_t card = ui->widget_vertical(host->ctx, fs, "review-fs-card");
		sk_ui_style_props_t cp;
		sk_ui_node_t row;
		sk_ui_node_t btn;
		memset(&cp, 0, sizeof(cp));
		cp.mask = SK_UI_SP_ALIGN_ITEMS | SK_UI_SP_ROW_GAP | SK_UI_SP_WIDTH;
		cp.layout.align_items = SK_UI_ALIGN_CENTER;
		cp.layout.row_gap = 16.0f;
		cp.layout.width = sk_ui_percent(100.0f);
		(void)ui->node_merge_inline_style(host->ctx, card, &cp);
		title = ui->widget_text(host->ctx, card, "Open a project", "review-fs-title");
		(void)title;
		row = ui->widget_horizontal(host->ctx, card, "review-fs-row");
		(void)ui->widget_spring(host->ctx, row, 1.0f, "review-fs-ls");
		btn = ui->widget_button(host->ctx, row, "New Project", "review-fs-new");
		(void)ui->button_set_size(host->ctx, btn, 160.0f, 0.0f);
		(void)ui->widget_spring(host->ctx, row, 1.0f, "review-fs-rs");
	}
	(void)ui->widget_spring(host->ctx, fs, 1.0f, "review-fs-bot");
	*out_target = fs;
	return 0;
}

static i32 sandbox_widget_build_child(const sandbox_widget_host_t* host, sk_ui_node_t* out_target) {
	const sk_ui_api_t* ui = host->ui;
	sk_ui_node_t root = ui->context_root(host->ctx);
	sk_ui_node_t col;
	sk_ui_node_t bar;
	sk_ui_node_t body;
	sk_ui_node_t content;
	sk_ui_style_props_t p;
	u32 i;

	sandbox_widget_style_fill(host);
	col = ui->widget_vertical(host->ctx, root, "review-child-col");
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_FLEX_GROW;
	p.layout.width = sk_ui_percent(100.0f);
	p.layout.height = sk_ui_percent(100.0f);
	p.layout.flex_grow = 1.0f;
	(void)ui->node_merge_inline_style(host->ctx, col, &p);

	/* Fixed-height toolbar + remaining-size bordered scroll (Console). */
	bar = ui->widget_child(host->ctx, col, "review-child-bar", 0.0f, 28.0f, SK_UI_CHILD_FLAG_NONE);
	{
		sk_ui_node_t row = ui->widget_horizontal(host->ctx, ui->child_content(host->ctx, bar), "review-child-bar-row");
		sk_ui_node_t clear = ui->widget_button(host->ctx, row, "Clear", "review-child-clear");
		(void)ui->button_set_size(host->ctx, clear, 64.0f, 0.0f);
	}
	body = ui->widget_child(host->ctx, col, "review-child-body", 0.0f, 0.0f, SK_UI_CHILD_FLAG_BORDER | SK_UI_CHILD_FLAG_HORIZONTAL_SCROLLBAR);
	content = ui->child_content(host->ctx, body);
	if (sk_ui_node_is_valid(content)) {
		for (i = 0u; i < 8u; ++i) {
			char line[56];
			(void)snprintf(line, sizeof(line), "log line %u  long enough to hint h-scroll", i + 1u);
			(void)ui->widget_text(host->ctx, content, line, NULL);
		}
		(void)ui->scroll_view_set_content_size(host->ctx, body, 520.0f, 180.0f);
	}
	*out_target = body;
	return 0;
}

static i32 sandbox_widget_build_child_resize(const sandbox_widget_host_t* host, sk_ui_node_t* out_target) {
	const sk_ui_api_t* ui = host->ui;
	sk_ui_node_t root = ui->context_root(host->ctx);
	sk_ui_node_t row;
	sk_ui_node_t left;
	sk_ui_node_t right;
	sk_ui_style_props_t p;

	sandbox_widget_style_fill(host);
	row = ui->widget_horizontal(host->ctx, root, "review-rx-row");
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_ALIGN_ITEMS;
	p.layout.width = sk_ui_percent(100.0f);
	p.layout.height = sk_ui_pt((f32)host->height - 32.0f);
	p.layout.align_items = SK_UI_ALIGN_STRETCH;
	(void)ui->node_merge_inline_style(host->ctx, row, &p);

	/* ResourceDebugger left pane: Borders | ResizeX. */
	left = ui->widget_child(host->ctx, row, "review-rx", 160.0f, 0.0f, SK_UI_CHILD_FLAG_BORDER | SK_UI_CHILD_FLAG_RESIZE_X);
	(void)ui->widget_text(host->ctx, ui->child_content(host->ctx, left), "Types", "review-rx-label");
	right = ui->widget_child(host->ctx, row, "review-rx-right", 0.0f, 0.0f, SK_UI_CHILD_FLAG_BORDER);
	(void)ui->widget_text(host->ctx, ui->child_content(host->ctx, right), "Instance", "review-rx-inst");
	*out_target = left;
	return 0;
}

static i32 sandbox_widget_build_layout(const sandbox_widget_host_t* host, sk_ui_node_t* out_target) {
	const sk_ui_api_t* ui = host->ui;
	sk_ui_node_t root = ui->context_root(host->ctx);
	sk_ui_node_t col;
	sk_ui_node_t row;
	sk_ui_node_t g;
	sk_ui_style_props_t p;

	sandbox_widget_style_fill(host);
	col = ui->widget_vertical(host->ctx, root, "review-layout");
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_ROW_GAP;
	p.layout.width = sk_ui_percent(100.0f);
	p.layout.row_gap = 8.0f;
	(void)ui->node_merge_inline_style(host->ctx, col, &p);

	/* Property row: label | Spring | value (FieldRenderers). */
	row = ui->widget_horizontal(host->ctx, col, "review-prop");
	(void)ui->widget_text(host->ctx, row, "Mass", "review-prop-lab");
	(void)ui->widget_spring(host->ctx, row, 1.0f, "review-prop-spring");
	{
		sk_ui_node_t sl = ui->widget_slider(host->ctx, row, 0.0f, 10.0f, 2.5f, "review-prop-val");
		sk_ui_style_props_t slp;
		memset(&slp, 0, sizeof(slp));
		slp.mask = SK_UI_SP_WIDTH | SK_UI_SP_MIN_WIDTH | SK_UI_SP_MAX_WIDTH | SK_UI_SP_HEIGHT;
		slp.layout.width = sk_ui_pt(160.0f);
		slp.layout.min_width = sk_ui_pt(160.0f);
		slp.layout.max_width = sk_ui_pt(160.0f);
		slp.layout.height = sk_ui_pt(18.0f);
		(void)ui->node_merge_inline_style(host->ctx, sl, &slp);
	}

	g = ui->widget_group(host->ctx, col, "review-group");
	{
		sk_ui_style_props_t gp;
		memset(&gp, 0, sizeof(gp));
		gp.mask = SK_UI_SP_PADDING;
		gp.layout.padding.left = SK_UI_INDENT_DEFAULT;
		(void)ui->node_merge_inline_style(host->ctx, g, &gp);
	}
	(void)ui->widget_text(host->ctx, g, "indented child", "review-indented");
	*out_target = col;
	return 0;
}

static i32 sandbox_widget_build_disabled(const sandbox_widget_host_t* host, sk_ui_node_t* out_target) {
	const sk_ui_api_t* ui = host->ui;
	sk_ui_node_t root = ui->context_root(host->ctx);
	sk_ui_node_t hostn;
	sk_ui_node_t btn;

	sandbox_widget_style_stage(host);
	hostn = ui->widget_group(host->ctx, root, "review-dis-host");
	(void)ui->begin_disabled(host->ctx, 1);
	btn = ui->widget_button(host->ctx, hostn, "Visibility", "review-dis-btn");
	(void)ui->widget_text(host->ctx, hostn, "read-only field", "review-dis-txt");
	(void)ui->end_disabled(host->ctx);
	(void)ui->set_disabled(host->ctx, hostn, 1);
	if (!sk_ui_node_is_valid(btn)) {
		fprintf(stderr, "sk-sandbox: disabled subtree failed\n");
		return -1;
	}
	*out_target = hostn;
	return 0;
}

static i32 sandbox_widget_build_drag_float3(const sandbox_widget_host_t* host, sk_ui_node_t* out_target) {
	const sk_ui_api_t* ui = host->ui;
	sk_ui_node_t root = ui->context_root(host->ctx);
	sk_ui_node_t n;
	f32 v[3] = {0.10f, 0.50f, 0.90f};

	sandbox_widget_style_stage(host);
	/* PropertiesWindow: DragFloat2/3/4. */
	n = ui->widget_drag_float_n(host->ctx, root, 3, 0.01f, 0.0f, 1.0f, v, "%.3f", "review-drag-f3");
	if (!sk_ui_node_is_valid(n)) {
		fprintf(stderr, "sk-sandbox: widget_drag_float_n failed\n");
		return -1;
	}
	sandbox_slider_size(host, n, 360.0f, 22.0f);
	*out_target = n;
	return 0;
}

typedef enum sandbox_menu_scene_t { SANDBOX_MENU_BAR = 0, SANDBOX_MENU_OPEN, SANDBOX_MENU_SUBMENU, SANDBOX_MENU_POPUP } sandbox_menu_scene_t;

static i32 sandbox_widget_build_menu_scene(const sandbox_widget_host_t* host, sk_ui_node_t* out_target, sandbox_menu_scene_t scene) {
	const sk_ui_api_t* ui = host->ui;
	sk_ui_node_t root = ui->context_root(host->ctx);
	sk_ui_node_t bar;
	sk_ui_node_t file_m;
	sk_ui_node_t edit_m;
	sk_ui_node_t popup;
	sk_ui_node_t open_it;
	sk_ui_node_t save;
	sk_ui_node_t recent;
	sk_ui_node_t locked;
	sk_ui_node_t grid;
	sk_ui_node_t target;

	sandbox_widget_style_fill(host);

	if (scene == SANDBOX_MENU_POPUP) {
		sk_ui_node_t hostn;
		sk_ui_node_t pop;
		sk_ui_layout_style_t ls;
		hostn = ui->widget_view(host->ctx, root, "review-menu-pop-host");
		if (!sk_ui_node_is_valid(hostn)) {
			fprintf(stderr, "sk-sandbox: menu popup host failed\n");
			return -1;
		}
		(void)ui->node_get_layout_style(host->ctx, hostn, &ls);
		ls.position = SK_UI_POSITION_RELATIVE;
		(void)ui->node_set_layout_style(host->ctx, hostn, &ls);
		pop = ui->widget_menu_popup(host->ctx, hostn, 0, "review-workspace-popup");
		if (!sk_ui_node_is_valid(pop)) {
			fprintf(stderr, "sk-sandbox: widget_menu_popup failed\n");
			return -1;
		}
		(void)ui->menu_set_open(host->ctx, pop, 1);
		(void)ui->widget_menu_item(host->ctx, pop, "Scene", "review-pop-scene");
		save = ui->widget_menu_item(host->ctx, pop, "Game", "review-pop-game");
		(void)ui->menu_item_set_shortcut(host->ctx, save, "Ctrl+2");
		(void)ui->widget_menu_separator(host->ctx, pop, "review-pop-sep");
		locked = ui->widget_menu_item(host->ctx, pop, "Locked Type", "review-pop-locked");
		(void)ui->menu_item_set_enabled(host->ctx, locked, 0);
		grid = ui->widget_menu_item(host->ctx, pop, "Default Workspace", "review-pop-def");
		(void)ui->menu_item_set_selected(host->ctx, grid, 1);
		*out_target = pop;
		return 0;
	}

	bar = ui->widget_menu_bar(host->ctx, root, "review-menubar");
	if (!sk_ui_node_is_valid(bar)) {
		fprintf(stderr, "sk-sandbox: widget_menu_bar failed\n");
		return -1;
	}
	file_m = ui->widget_menu(host->ctx, bar, "File", "review-menu-file");
	edit_m = ui->widget_menu(host->ctx, bar, "Edit", "review-menu-edit");
	(void)ui->widget_menu(host->ctx, bar, "Window", "review-menu-window");
	(void)ui->menu_set_enabled(host->ctx, edit_m, 0);

	popup = ui->menu_get_popup(host->ctx, file_m);
	open_it = ui->widget_menu_item(host->ctx, popup, "Open", "review-menu-open");
	(void)ui->menu_item_set_shortcut(host->ctx, open_it, "Ctrl+O");
	save = ui->widget_menu_item(host->ctx, popup, "Save", "review-menu-save");
	(void)ui->menu_item_set_shortcut(host->ctx, save, "Ctrl+S");
	(void)ui->widget_menu_separator(host->ctx, popup, "review-menu-sep");
	recent = ui->widget_submenu(host->ctx, popup, "Recent", "review-menu-recent");
	{
		sk_ui_node_t sub = ui->menu_get_popup(host->ctx, recent);
		sk_ui_node_t a = ui->widget_menu_item(host->ctx, sub, "Project A", "review-menu-a");
		(void)ui->widget_menu_item(host->ctx, sub, "Project B", "review-menu-b");
		(void)a;
	}
	locked = ui->widget_menu_item(host->ctx, popup, "Export (disabled)", "review-menu-export");
	(void)ui->menu_item_set_enabled(host->ctx, locked, 0);
	grid = ui->widget_menu_item(host->ctx, popup, "Show Grid", "review-menu-grid");
	(void)ui->menu_item_set_selected(host->ctx, grid, 1);

	if (scene == SANDBOX_MENU_OPEN || scene == SANDBOX_MENU_SUBMENU) {
		(void)ui->menu_set_open(host->ctx, file_m, 1);
	}
	if (scene == SANDBOX_MENU_SUBMENU) {
		(void)ui->menu_set_open(host->ctx, recent, 1);
	}

	if (scene == SANDBOX_MENU_BAR) {
		target = file_m;
	} else if (scene == SANDBOX_MENU_SUBMENU) {
		target = recent;
	} else {
		target = save;
	}
	*out_target = target;
	return 0;
}

static i32 sandbox_widget_build_menu(const sandbox_widget_host_t* host, sk_ui_node_t* out_target) {
	return sandbox_widget_build_menu_scene(host, out_target, SANDBOX_MENU_BAR);
}

static i32 sandbox_widget_build_menu_open(const sandbox_widget_host_t* host, sk_ui_node_t* out_target) {
	return sandbox_widget_build_menu_scene(host, out_target, SANDBOX_MENU_OPEN);
}

static i32 sandbox_widget_build_menu_submenu(const sandbox_widget_host_t* host, sk_ui_node_t* out_target) {
	return sandbox_widget_build_menu_scene(host, out_target, SANDBOX_MENU_SUBMENU);
}

static i32 sandbox_widget_build_menu_popup(const sandbox_widget_host_t* host, sk_ui_node_t* out_target) {
	return sandbox_widget_build_menu_scene(host, out_target, SANDBOX_MENU_POPUP);
}

static void sandbox_widget_fill_editor_bg(const sandbox_widget_host_t* host) {
	const sk_ui_api_t* ui = host->ui;
	sk_ui_node_t root = ui->context_root(host->ctx);
	sk_ui_style_props_t p;
	sk_ui_node_t bar;
	sk_ui_node_t pane;

	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_FLEX_DIRECTION | SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_BACKGROUND_COLOR;
	p.layout.flex_direction = SK_UI_FLEX_COLUMN;
	p.layout.width = sk_ui_pt((f32)host->width);
	p.layout.height = sk_ui_pt((f32)host->height);
	p.background_color = sk_ui_rgba(0.10f, 0.11f, 0.13f, 1.0f);
	(void)ui->node_set_inline_style(host->ctx, root, &p);

	bar = ui->widget_menu_bar(host->ctx, root, "review-ed-bar");
	(void)ui->widget_menu(host->ctx, bar, "File", "review-ed-file");
	(void)ui->widget_menu(host->ctx, bar, "Edit", "review-ed-edit");
	(void)ui->widget_menu(host->ctx, bar, "Window", "review-ed-window");

	pane = ui->widget_view(host->ctx, root, "review-ed-pane");
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_FLEX_GROW | SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_PADDING | SK_UI_SP_BACKGROUND_COLOR | SK_UI_SP_FLEX_DIRECTION;
	p.layout.flex_grow = 1.0f;
	p.layout.width = sk_ui_percent(100.0f);
	p.layout.height = sk_ui_auto();
	p.layout.padding.left = 12.0f;
	p.layout.padding.top = 12.0f;
	p.layout.padding.right = 12.0f;
	p.layout.padding.bottom = 12.0f;
	p.background_color = sk_ui_rgba(0.13f, 0.14f, 0.16f, 1.0f);
	p.layout.flex_direction = SK_UI_FLEX_COLUMN;
	(void)ui->node_merge_inline_style(host->ctx, pane, &p);
	(void)ui->widget_text(host->ctx, pane, "Hierarchy", "review-ed-h");
	(void)ui->widget_text(host->ctx, pane, "  Entity_01", "review-ed-e1");
	(void)ui->widget_text(host->ctx, pane, "  Entity_02", "review-ed-e2");
	(void)ui->widget_button(host->ctx, pane, "Add Component", "review-ed-add");
}

static i32 sandbox_widget_build_popup_menu(const sandbox_widget_host_t* host, sk_ui_node_t* out_target) {
	const sk_ui_api_t* ui = host->ui;
	sk_ui_node_t root = ui->context_root(host->ctx);
	sk_ui_node_t menu;
	sk_ui_node_t del;
	sk_ui_layout_style_t ls;

	sandbox_widget_fill_editor_bg(host);
	menu = ui->widget_popup_menu(host->ctx, root, "review-ctx-menu");
	if (!sk_ui_node_is_valid(menu)) {
		fprintf(stderr, "sk-sandbox: widget_popup_menu failed\n");
		return -1;
	}
	(void)ui->node_get_layout_style(host->ctx, menu, &ls);
	ls.position = SK_UI_POSITION_ABSOLUTE;
	ls.left = sk_ui_pt(48.0f);
	ls.top = sk_ui_pt(64.0f);
	(void)ui->node_set_layout_style(host->ctx, menu, &ls);
	(void)ui->popup_open(host->ctx, menu);
	(void)ui->widget_menu_item(host->ctx, menu, "Rename", "review-ctx-rename");
	(void)ui->widget_menu_item(host->ctx, menu, "Duplicate", "review-ctx-dup");
	(void)ui->widget_menu_separator(host->ctx, menu, "review-ctx-sep");
	del = ui->widget_menu_item(host->ctx, menu, "Delete", "review-ctx-del");
	(void)ui->menu_item_set_shortcut(host->ctx, del, "Del");
	*out_target = menu;
	return 0;
}

static i32 sandbox_widget_build_popup(const sandbox_widget_host_t* host, sk_ui_node_t* out_target) {
	const sk_ui_api_t* ui = host->ui;
	sk_ui_node_t root = ui->context_root(host->ctx);
	sk_ui_node_t modal;
	sk_ui_node_t body;
	sk_ui_node_t row;
	sk_ui_node_t ok;
	sk_ui_node_t close;

	sandbox_widget_fill_editor_bg(host);
	modal = ui->widget_modal(host->ctx, root, "Cannot delete", "review-modal", NULL, SK_UI_MODAL_FLAG_ALWAYS_AUTO_RESIZE);
	if (!sk_ui_node_is_valid(modal)) {
		fprintf(stderr, "sk-sandbox: widget_modal failed\n");
		return -1;
	}
	body = ui->modal_body(host->ctx, modal);
	(void)ui->widget_text(host->ctx, body, "Entity is referenced by 2 assets.", "review-modal-msg");
	row = ui->modal_button_row(host->ctx, modal);
	ok = ui->widget_button(host->ctx, row, "OK", "review-modal-ok");
	close = ui->widget_button(host->ctx, row, "Close", "review-modal-close");
	(void)ui->button_set_size(host->ctx, ok, 120.0f, 0.0f);
	(void)ui->button_set_size(host->ctx, close, 120.0f, 0.0f);
	(void)ui->set_item_default_focus(host->ctx, ok);
	(void)ui->popup_open(host->ctx, modal);
	*out_target = ok;
	return 0;
}

static i32 sandbox_widget_build_modal_save(const sandbox_widget_host_t* host, sk_ui_node_t* out_target) {
	const sk_ui_api_t* ui = host->ui;
	sk_ui_node_t root = ui->context_root(host->ctx);
	sk_ui_node_t modal;
	sk_ui_node_t body;
	sk_ui_node_t child;
	sk_ui_node_t content;
	sk_ui_node_t row;
	sk_ui_node_t save;
	sk_ui_node_t dont;
	sk_ui_node_t cancel;
	static i32 s_open = 1;
	u32 i;

	sandbox_widget_fill_editor_bg(host);
	s_open = 1;
	modal = ui->widget_modal(host->ctx, root, "Save Content", "review-save", &s_open, SK_UI_MODAL_FLAG_NO_SCROLLBAR);
	if (!sk_ui_node_is_valid(modal)) {
		fprintf(stderr, "sk-sandbox: widget_modal (save) failed\n");
		return -1;
	}
	body = ui->modal_body(host->ctx, modal);
	child = ui->widget_child(host->ctx, body, "review-save-child", 0.0f, 0.0f, SK_UI_CHILD_FLAG_BORDER);
	content = ui->child_content(host->ctx, child);
	if (!sk_ui_node_is_valid(content)) {
		content = body;
	}
	{
		const_chr_t rows[4] = {"scene.skore", "material.mat", "mesh.bin", "anim.clip"};
		for (i = 0u; i < 4u; ++i) {
			char id[32];
			(void)snprintf(id, sizeof(id), "review-save-r%u", i);
			(void)ui->widget_text(host->ctx, content, rows[i], id);
		}
	}
	row = ui->modal_button_row(host->ctx, modal);
	save = ui->widget_button(host->ctx, row, "Save", "review-save-ok");
	dont = ui->widget_button(host->ctx, row, "Don't Save", "review-save-dont");
	cancel = ui->widget_button(host->ctx, row, "Cancel", "review-save-cancel");
	(void)ui->button_set_size(host->ctx, save, 120.0f, 0.0f);
	(void)ui->button_set_size(host->ctx, dont, 120.0f, 0.0f);
	(void)ui->button_set_size(host->ctx, cancel, 120.0f, 0.0f);
	(void)ui->set_item_default_focus(host->ctx, save);
	*out_target = save;
	return 0;
}

static i32 sandbox_widget_build_tab_bar(const sandbox_widget_host_t* host, sk_ui_node_t* out_target) {
	const sk_ui_api_t* ui = host->ui;
	sk_ui_node_t root = ui->context_root(host->ctx);
	sk_ui_node_t col;
	sk_ui_node_t bar;
	sk_ui_node_t scene;
	sk_ui_node_t game;
	sk_ui_node_t plus;
	sk_ui_node_t body;
	sk_ui_style_props_t p;
	static i32 s_scene_open = 1;
	static i32 s_selected = 0;

	sandbox_widget_style_fill(host);
	s_scene_open = 1;
	s_selected = 0;
	col = ui->widget_vertical(host->ctx, root, "review-tab-col");
	if (!sk_ui_node_is_valid(col)) {
		fprintf(stderr, "sk-sandbox: tab column failed\n");
		return -1;
	}
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_FLEX_DIRECTION;
	p.layout.width = sk_ui_pt((f32)host->width - 32.0f);
	p.layout.height = sk_ui_pt((f32)host->height - 32.0f);
	p.layout.flex_direction = SK_UI_FLEX_COLUMN;
	(void)ui->node_merge_inline_style(host->ctx, col, &p);

	bar = ui->widget_tab_bar(host->ctx, col, "review-tab-bar");
	if (!sk_ui_node_is_valid(bar)) {
		fprintf(stderr, "sk-sandbox: widget_tab_bar failed\n");
		return -1;
	}
	scene = ui->widget_tab_item(host->ctx, bar, "Scene", "review-tab-scene", &s_scene_open, SK_UI_TAB_ITEM_FLAG_NONE);
	game = ui->widget_tab_item(host->ctx, bar, "Game", "review-tab-game", NULL, SK_UI_TAB_ITEM_FLAG_NONE);
	plus = ui->widget_tab_button(host->ctx, bar, "+", "review-tab-plus");
	if (!sk_ui_node_is_valid(scene) || !sk_ui_node_is_valid(game) || !sk_ui_node_is_valid(plus)) {
		fprintf(stderr, "sk-sandbox: tab items failed\n");
		return -1;
	}
	(void)ui->tab_bar_bind_selected(host->ctx, bar, &s_selected);
	(void)ui->tab_bar_set_selected(host->ctx, bar, 0);
	body = ui->tab_body(host->ctx, scene);
	(void)ui->widget_text(host->ctx, body, "Scene workspace", "review-tab-body-txt");
	(void)ui->tab_body(host->ctx, game);
	*out_target = game;
	return 0;
}

static i32 sandbox_widget_build_tab_plus(const sandbox_widget_host_t* host, sk_ui_node_t* out_target) {
	const sk_ui_api_t* ui = host->ui;
	sk_ui_node_t root = ui->context_root(host->ctx);
	sk_ui_node_t bar;
	sk_ui_node_t plus;
	sk_ui_node_t t0;

	sandbox_widget_style_stage(host);
	bar = ui->widget_tab_bar(host->ctx, root, "review-tab-plus-bar");
	if (!sk_ui_node_is_valid(bar)) {
		fprintf(stderr, "sk-sandbox: tab plus bar failed\n");
		return -1;
	}
	t0 = ui->widget_tab(host->ctx, bar, "Scene", "review-plus-scene");
	plus = ui->widget_tab_button(host->ctx, bar, "+", "review-plus-btn");
	if (!sk_ui_node_is_valid(plus) || !sk_ui_node_is_valid(t0)) {
		fprintf(stderr, "sk-sandbox: tab plus button failed\n");
		return -1;
	}
	(void)ui->tab_bar_set_selected(host->ctx, bar, 0);
	*out_target = plus;
	return 0;
}

/* ---- separator / spacing / same-line family (APX-349) ---- */

static i32 sandbox_widget_build_separator(const sandbox_widget_host_t* host, sk_ui_node_t* out_target) {
	const sk_ui_api_t* ui = host->ui;
	sk_ui_node_t root = ui->context_root(host->ctx);
	sk_ui_node_t toolbar;
	sk_ui_node_t group;
	sk_ui_node_t matrix;
	sk_ui_node_t bar;
	sk_ui_node_t target = SK_UI_NODE_INVALID;

	sandbox_widget_style_fill(host);

	/* Menu bar: Separator inside a horizontal layout renders VERTICAL. */
	bar = ui->widget_menu_bar(host->ctx, root, "review-sep-menubar");
	if (!sk_ui_node_is_valid(bar)) {
		fprintf(stderr, "sk-sandbox: separator menu bar failed\n");
		return -1;
	}
	(void)ui->widget_menu(host->ctx, bar, "File", "review-sep-file");
	(void)ui->widget_menu(host->ctx, bar, "Edit", "review-sep-edit");
	(void)ui->widget_separator(host->ctx, bar, "review-sep-bar");
	(void)ui->widget_menu(host->ctx, bar, "Help", "review-sep-help");

	/* ConsoleWindow.cpp:44-70 toolbar: Clear | vertical rule | checkbox
	 * group | vertical rule | Collapse | Auto-scroll. Tight icon-button run
	 * (SameLine(0, 0)) at the front shows the icon-chrome packing. */
	toolbar = ui->widget_panel(host->ctx, root, "review-toolbar");
	if (!sk_ui_node_is_valid(toolbar)) {
		fprintf(stderr, "sk-sandbox: separator toolbar failed\n");
		return -1;
	}
	target = toolbar;
	/* Fixed 40px toolbar lane so the stage below it is not pushed by the
	 * flex-measured (pre-SameLine) column height. */
	{
		sk_ui_style_props_t tp;
		memset(&tp, 0, sizeof(tp));
		tp.mask = SK_UI_SP_HEIGHT | SK_UI_SP_MIN_HEIGHT | SK_UI_SP_MAX_HEIGHT;
		tp.layout.height = sk_ui_pt(36.0f);
		tp.layout.min_height = sk_ui_pt(36.0f);
		tp.layout.max_height = sk_ui_pt(36.0f);
		(void)ui->node_merge_inline_style(host->ctx, toolbar, &tp);
	}
	(void)ui->widget_small_button(host->ctx, toolbar, "Play", "review-tb-play");
	(void)ui->widget_same_line(host->ctx, toolbar, 0.0f, 0.0f);
	(void)ui->widget_small_button(host->ctx, toolbar, "Stop", "review-tb-stop");
	(void)ui->widget_same_line(host->ctx, toolbar, 0.0f, 0.0f);
	(void)ui->widget_small_button(host->ctx, toolbar, "Pause", "review-tb-pause");
	(void)ui->widget_same_line(host->ctx, toolbar, 0.0f, 12.0f);
	(void)ui->widget_small_button(host->ctx, toolbar, "Clear", "review-tb-clear");
	(void)ui->widget_same_line(host->ctx, toolbar, 0.0f, -1.0f);
	(void)ui->widget_separator(host->ctx, toolbar, "review-tb-sep1");
	(void)ui->widget_same_line(host->ctx, toolbar, 0.0f, -1.0f);
	group = ui->widget_group(host->ctx, toolbar, "review-tb-group");
	if (!sk_ui_node_is_valid(group)) {
		fprintf(stderr, "sk-sandbox: separator group failed\n");
		return -1;
	}
	(void)ui->widget_checkbox(host->ctx, group, 1, "review-tb-trace");
	(void)ui->widget_same_line(host->ctx, group, 0.0f, -1.0f);
	(void)ui->widget_checkbox(host->ctx, group, 0, "review-tb-debug");
	(void)ui->widget_same_line(host->ctx, group, 0.0f, -1.0f);
	(void)ui->widget_checkbox(host->ctx, group, 0, "review-tb-info");
	(void)ui->widget_same_line(host->ctx, group, 0.0f, -1.0f);
	(void)ui->widget_checkbox(host->ctx, group, 0, "review-tb-warn");
	(void)ui->widget_same_line(host->ctx, group, 0.0f, -1.0f);
	(void)ui->widget_checkbox(host->ctx, group, 0, "review-tb-error");
	(void)ui->widget_same_line(host->ctx, toolbar, 0.0f, -1.0f);
	(void)ui->widget_separator(host->ctx, toolbar, "review-tb-sep2");
	(void)ui->widget_same_line(host->ctx, toolbar, 0.0f, -1.0f);
	(void)ui->widget_checkbox(host->ctx, toolbar, 0, "review-tb-collapse");
	(void)ui->widget_same_line(host->ctx, toolbar, 0.0f, -1.0f);
	(void)ui->widget_checkbox(host->ctx, toolbar, 0, "review-tb-autoscroll");
	/* Full-width horizontal rule directly below the toolbar row. */
	(void)ui->widget_separator(host->ctx, toolbar, "review-tb-hsep");

	/* Spacing + Dummy(0, 2) vertical spacers between sections. */
	(void)ui->widget_spacing(host->ctx, root, "review-spacing");
	(void)ui->widget_dummy(host->ctx, root, 0.0f, 2.0f, "review-dummy");

	/* SettingsWindow.cpp:326 collision matrix: SameLine(labelWidth + k*cell)
	 * absolute cells on one row. */
	matrix = ui->widget_vertical(host->ctx, root, "review-matrix");
	if (!sk_ui_node_is_valid(matrix)) {
		fprintf(stderr, "sk-sandbox: separator matrix failed\n");
		return -1;
	}
	(void)ui->widget_text(host->ctx, matrix, "Default", "review-mlab");
	(void)ui->widget_same_line(host->ctx, matrix, 80.0f, -1.0f);
	(void)ui->widget_checkbox(host->ctx, matrix, 1, "review-m1");
	(void)ui->widget_same_line(host->ctx, matrix, 104.0f, -1.0f);
	(void)ui->widget_checkbox(host->ctx, matrix, 0, "review-m2");
	(void)ui->widget_same_line(host->ctx, matrix, 128.0f, -1.0f);
	(void)ui->widget_checkbox(host->ctx, matrix, 0, "review-m3");
	(void)ui->widget_text(host->ctx, matrix, "Static", "review-mlab2");
	(void)ui->widget_same_line(host->ctx, matrix, 80.0f, -1.0f);
	(void)ui->widget_checkbox(host->ctx, matrix, 0, "review-m4");
	(void)ui->widget_same_line(host->ctx, matrix, 104.0f, -1.0f);
	(void)ui->widget_checkbox(host->ctx, matrix, 1, "review-m5");
	(void)ui->widget_same_line(host->ctx, matrix, 128.0f, -1.0f);
	(void)ui->widget_checkbox(host->ctx, matrix, 0, "review-m6");

	{
		sk_ui_rect_t r;
		if (ui->node_get_abs_rect(host->ctx, matrix, &r, NULL) == 0) {
			printf("DBG matrix x=%d y=%d w=%d h=%d\n", (int)r.x, (int)r.y, (int)r.width, (int)r.height);
		}
		if (ui->node_get_abs_rect(host->ctx, ui->context_root(host->ctx), &r, NULL) == 0) {
			printf("DBG root x=%d y=%d w=%d h=%d\n", (int)r.x, (int)r.y, (int)r.width, (int)r.height);
		}
	}
	*out_target = target;
	return 0;
}

static sk_ui_item_t s_review_tree_items[8];
static sk_ui_item_array_t s_review_tree_arr;
static sk_ui_item_t s_review_deep_items[8];
static sk_ui_item_array_t s_review_deep_arr;

static void sandbox_tree_size(const sandbox_widget_host_t* host, sk_ui_node_t node, f32 w, f32 h) {
	const sk_ui_api_t* ui = host->ui;
	sk_ui_style_props_t p;
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT;
	p.layout.width = sk_ui_pt(w);
	p.layout.height = sk_ui_pt(h);
	(void)ui->node_merge_inline_style(host->ctx, node, &p);
}

static i32 sandbox_widget_build_tree(const sandbox_widget_host_t* host, sk_ui_node_t* out_target) {
	const sk_ui_api_t* ui = host->ui;
	sk_ui_node_t root = ui->context_root(host->ctx);
	sk_ui_node_t host_tree;
	sk_ui_node_t row;

	sandbox_widget_style_fill(host);
	sk_ui_item_set(&s_review_tree_items[0], 1ull, 0ull, "Scene", (u32)SK_UI_ITEM_FLAG_OPEN);
	sk_ui_item_set(&s_review_tree_items[1], 2ull, 1ull, "Camera", (u32)SK_UI_ITEM_FLAG_OPEN);
	sk_ui_item_set(&s_review_tree_items[2], 3ull, 2ull, "Lens", (u32)SK_UI_ITEM_FLAG_LEAF);
	sk_ui_item_set(&s_review_tree_items[3], 4ull, 1ull, "Player", (u32)SK_UI_ITEM_FLAG_OPEN | (u32)SK_UI_ITEM_FLAG_SELECTED);
	sk_ui_item_set(&s_review_tree_items[4], 5ull, 4ull, "Mesh", (u32)SK_UI_ITEM_FLAG_LEAF);
	sk_ui_item_set(&s_review_tree_items[5], 6ull, 1ull, "Light", (u32)SK_UI_ITEM_FLAG_LEAF | (u32)SK_UI_ITEM_FLAG_DISABLED);
	s_review_tree_arr.items = s_review_tree_items;
	s_review_tree_arr.count = 6u;
	s_review_tree_arr.revision = 1u;

	host_tree = ui->widget_tree(host->ctx, root, &s_review_tree_arr, "review-tree");
	if (!sk_ui_node_is_valid(host_tree)) {
		fprintf(stderr, "sk-sandbox: widget_tree failed\n");
		return -1;
	}
	(void)ui->item_bind_set_flags(host->ctx, host_tree, SK_UI_TREE_NODE_FLAGS_DEFAULT | SK_UI_TREE_NODE_FLAG_OPEN_ON_DOUBLE_CLICK);
	(void)ui->item_bind_set_open(host->ctx, host_tree, 1ull, 1);
	(void)ui->item_bind_set_open(host->ctx, host_tree, 4ull, 1);
	(void)ui->item_bind_set_selected(host->ctx, host_tree, 4ull, 1);
	sandbox_tree_size(host, host_tree, (f32)host->width - 32.0f, (f32)host->height - 32.0f);
	row = ui->item_bind_find(host->ctx, host_tree, 4ull);
	*out_target = sk_ui_node_is_valid(row) ? row : host_tree;
	return 0;
}

static i32 sandbox_widget_build_collapsing_header(const sandbox_widget_host_t* host, sk_ui_node_t* out_target) {
	const sk_ui_api_t* ui = host->ui;
	sk_ui_node_t root = ui->context_root(host->ctx);
	sk_ui_node_t col;
	sk_ui_node_t xform;
	sk_ui_node_t mesh;
	sk_ui_node_t body;
	sk_ui_style_props_t p;

	sandbox_widget_style_fill(host);
	col = ui->widget_vertical(host->ctx, root, "review-ch-col");
	if (!sk_ui_node_is_valid(col)) {
		fprintf(stderr, "sk-sandbox: collapsing column failed\n");
		return -1;
	}
	memset(&p, 0, sizeof(p));
	p.mask = SK_UI_SP_WIDTH | SK_UI_SP_HEIGHT | SK_UI_SP_FLEX_DIRECTION;
	p.layout.width = sk_ui_pt((f32)host->width - 32.0f);
	p.layout.height = sk_ui_pt((f32)host->height - 32.0f);
	p.layout.flex_direction = SK_UI_FLEX_COLUMN;
	(void)ui->node_merge_inline_style(host->ctx, col, &p);

	(void)ui->set_next_item_open(host->ctx, 1, SK_UI_COND_ONCE);
	xform = ui->widget_collapsing_header(host->ctx, col, "Transform", "review-ch-xform", SK_UI_TREE_NODE_FLAG_TRAILING_BUTTON | SK_UI_TREE_NODE_FLAG_DEFAULT_OPEN);
	if (!sk_ui_node_is_valid(xform)) {
		fprintf(stderr, "sk-sandbox: collapsing_header failed\n");
		return -1;
	}
	body = ui->collapsing_header_body(host->ctx, xform);
	(void)ui->widget_text(host->ctx, body, "Position    0.00  1.20  -3.40", "review-ch-pos");
	(void)ui->widget_text(host->ctx, body, "Rotation    0.00  45.0   0.00", "review-ch-rot");
	mesh = ui->widget_collapsing_header(host->ctx, col, "Mesh Renderer", "review-ch-mesh", SK_UI_TREE_NODE_FLAG_TRAILING_BUTTON);
	if (!sk_ui_node_is_valid(mesh)) {
		fprintf(stderr, "sk-sandbox: mesh collapsing_header failed\n");
		return -1;
	}
	(void)ui->widget_text(host->ctx, ui->collapsing_header_body(host->ctx, mesh), "mesh.skmesh", "review-ch-mesh-txt");
	*out_target = xform;
	return 0;
}

static i32 sandbox_widget_build_tree_deep(const sandbox_widget_host_t* host, sk_ui_node_t* out_target) {
	const sk_ui_api_t* ui = host->ui;
	sk_ui_node_t root = ui->context_root(host->ctx);
	sk_ui_node_t host_tree;
	sk_ui_node_t leaf;
	u32 i;

	sandbox_widget_style_fill(host);
	sk_ui_item_set(&s_review_deep_items[0], 1ull, 0ull, "World", (u32)SK_UI_ITEM_FLAG_OPEN);
	sk_ui_item_set(&s_review_deep_items[1], 2ull, 1ull, "Level", (u32)SK_UI_ITEM_FLAG_OPEN);
	sk_ui_item_set(&s_review_deep_items[2], 3ull, 2ull, "Room", (u32)SK_UI_ITEM_FLAG_OPEN);
	sk_ui_item_set(&s_review_deep_items[3], 4ull, 3ull, "Props", (u32)SK_UI_ITEM_FLAG_OPEN);
	sk_ui_item_set(&s_review_deep_items[4], 5ull, 4ull, "Chest", (u32)SK_UI_ITEM_FLAG_OPEN);
	sk_ui_item_set(&s_review_deep_items[5], 6ull, 5ull, "Gold", (u32)SK_UI_ITEM_FLAG_LEAF | (u32)SK_UI_ITEM_FLAG_SELECTED);
	sk_ui_item_set(&s_review_deep_items[6], 7ull, 4ull, "Torch", (u32)SK_UI_ITEM_FLAG_LEAF);
	s_review_deep_arr.items = s_review_deep_items;
	s_review_deep_arr.count = 7u;
	s_review_deep_arr.revision = 1u;

	host_tree = ui->widget_tree(host->ctx, root, &s_review_deep_arr, "review-tree-deep");
	if (!sk_ui_node_is_valid(host_tree)) {
		fprintf(stderr, "sk-sandbox: deep widget_tree failed\n");
		return -1;
	}
	for (i = 1u; i <= 5u; ++i) {
		(void)ui->item_bind_set_open(host->ctx, host_tree, (u64)i, 1);
	}
	(void)ui->item_bind_set_selected(host->ctx, host_tree, 6ull, 1);
	sandbox_tree_size(host, host_tree, (f32)host->width - 32.0f, (f32)host->height - 32.0f);
	leaf = ui->item_bind_find(host->ctx, host_tree, 6ull);
	*out_target = sk_ui_node_is_valid(leaf) ? leaf : host_tree;
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
	{"checkbox", NULL, "§4 Checkbox", SANDBOX_WS_BITS_CHECKBOX, 320u, 96u, sandbox_widget_build_checkbox},
	{"radio", NULL, "§4 RadioButton", SANDBOX_WS_BITS_RADIO, 320u, 96u, sandbox_widget_build_radio},
	{"radio_group", "radiogroup", "§4 Radio group", SANDBOX_WS_BIT_DEFAULT, 320u, 160u, sandbox_widget_build_radio_group},
	{"text_input", "input", "§5 InputText", SANDBOX_WS_BITS_INTERACTIVE, 384u, 96u, sandbox_widget_build_text_input},
	{"text_input_hint", "inputhint", "§5 InputText hint / Search", SANDBOX_WS_BIT_DEFAULT | SANDBOX_WS_BIT_FOCUSED, 384u, 96u, sandbox_widget_build_text_input_hint},
	{"text_input_selection", "inputsel", "§5 InputText selection", SANDBOX_WS_BIT_FOCUSED, 384u, 96u, sandbox_widget_build_text_input_selection},
	{"text_input_multiline", "inputml", "§5 InputTextMultiline", SANDBOX_WS_BIT_DEFAULT | SANDBOX_WS_BIT_FOCUSED, 400u, 180u, sandbox_widget_build_text_input_multiline},
	{"text_input_readonly", "inputro", "§5 InputText ReadOnly", SANDBOX_WS_BIT_DEFAULT | SANDBOX_WS_BIT_FOCUSED | SANDBOX_WS_BIT_DISABLED, 400u, 96u,
	 sandbox_widget_build_text_input_readonly},
	{"slider", NULL, "§6 SliderFloat", SANDBOX_WS_BITS_SLIDER, 420u, 96u, sandbox_widget_build_slider},
	{"slider_int", "sliderint", "§6 SliderInt", SANDBOX_WS_BITS_SLIDER, 420u, 96u, sandbox_widget_build_slider_int},
	{"slider_float3", "sliderf3", "§6 SliderFloat3", SANDBOX_WS_BITS_SLIDER, 480u, 96u, sandbox_widget_build_slider_float3},
	{"drag_float", "drag", "§6 DragFloat", SANDBOX_WS_BITS_SLIDER, 420u, 96u, sandbox_widget_build_drag_float},
	{"drag_int", "dragint", "§6 DragInt", SANDBOX_WS_BITS_SLIDER, 420u, 96u, sandbox_widget_build_drag_int},
	{"drag_float3", "dragf3", "§6 DragFloat3", SANDBOX_WS_BITS_SLIDER, 480u, 96u, sandbox_widget_build_drag_float3},
	{"combo", "listbox", "§7 Combo / ListBox", SANDBOX_WS_BIT_DEFAULT | SANDBOX_WS_BIT_HOVERED | SANDBOX_WS_BIT_DISABLED | SANDBOX_WS_BIT_FOCUSED, 320u, 160u, NULL},
	{"tree", NULL, "§8 TreeNode Entity Tree", SANDBOX_WS_BIT_DEFAULT | SANDBOX_WS_BIT_HOVERED | SANDBOX_WS_BIT_DISABLED | SANDBOX_WS_BIT_FOCUSED, 384u, 256u,
	 sandbox_widget_build_tree},
	{"collapsing_header", "collapsing", "§8 CollapsingHeader + trailing button", SANDBOX_WS_BIT_DEFAULT | SANDBOX_WS_BIT_HOVERED | SANDBOX_WS_BIT_FOCUSED, 420u, 220u,
	 sandbox_widget_build_collapsing_header},
	{"tree_deep", "tree_selected", "§8 deep indent + selected", SANDBOX_WS_BIT_DEFAULT | SANDBOX_WS_BIT_HOVERED, 384u, 256u, sandbox_widget_build_tree_deep},
	{"table", NULL, "§9 Table", SANDBOX_WS_BIT_DEFAULT, 480u, 256u, NULL},
	{"tab_bar", "tab", "§10 TabBar workspace + close + body", SANDBOX_WS_BIT_DEFAULT | SANDBOX_WS_BIT_HOVERED | SANDBOX_WS_BIT_FOCUSED, 520u, 180u, sandbox_widget_build_tab_bar},
	{"tab_bar_plus", "tabplus", "§10 TabItemButton +", SANDBOX_WS_BIT_DEFAULT | SANDBOX_WS_BIT_HOVERED, 320u, 96u, sandbox_widget_build_tab_plus},
	{"menu", "menubar", "§11 MenuBar / Menu / MenuItem", SANDBOX_WS_BIT_DEFAULT | SANDBOX_WS_BIT_HOVERED | SANDBOX_WS_BIT_DISABLED | SANDBOX_WS_BIT_FOCUSED, 640u, 280u,
	 sandbox_widget_build_menu},
	{"menu_open", "menuopen", "§11 open File menu + shortcut/check/sep", SANDBOX_WS_BIT_DEFAULT | SANDBOX_WS_BIT_HOVERED | SANDBOX_WS_BIT_DISABLED, 640u, 280u,
	 sandbox_widget_build_menu_open},
	{"menu_submenu", "submenu", "§11 nested submenu", SANDBOX_WS_BIT_DEFAULT, 640u, 280u, sandbox_widget_build_menu_submenu},
	{"menu_popup", "menupopup", "§11 MenuItem inside popup", SANDBOX_WS_BIT_DEFAULT, 480u, 240u, sandbox_widget_build_menu_popup},
	{"popup", "modal", "§12 auto-resize modal + dim", SANDBOX_WS_BIT_DEFAULT, 560u, 360u, sandbox_widget_build_popup},
	{"popup_menu", "contextmenu", "§12 ImGuiBeginPopupMenu 300px", SANDBOX_WS_BIT_DEFAULT, 480u, 280u, sandbox_widget_build_popup_menu},
	{"modal_save", "savecontent", "§12 Save Content fixed child+table", SANDBOX_WS_BIT_DEFAULT, 640u, 400u, sandbox_widget_build_modal_save},
	{"window", NULL, "§13 named window + close", SANDBOX_WS_BIT_DEFAULT | SANDBOX_WS_BIT_DISABLED, 480u, 280u, sandbox_widget_build_window},
	{"fullscreen", NULL, "§13 ImGuiBeginFullscreen", SANDBOX_WS_BIT_DEFAULT, 480u, 280u, sandbox_widget_build_fullscreen},
	{"child", NULL, "§13 BeginChild remaining + border", SANDBOX_WS_BIT_DEFAULT, 480u, 280u, sandbox_widget_build_child},
	{"child_resize", "resizex", "§13 Child ResizeX", SANDBOX_WS_BIT_DEFAULT, 480u, 280u, sandbox_widget_build_child_resize},
	{"layout", NULL, "§13 horizontal + Spring", SANDBOX_WS_BIT_DEFAULT, 480u, 200u, sandbox_widget_build_layout},
	{"disabled", NULL, "§13 BeginDisabled subtree", SANDBOX_WS_BIT_DISABLED, 320u, 128u, sandbox_widget_build_disabled},
	{"separator", NULL, "§19 Separator / Spacing / SameLine", SANDBOX_WS_BIT_DEFAULT, 760u, 280u, sandbox_widget_build_separator},
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
	printf("aliases: label=text input=text_input listbox=combo tab=tab_bar tabplus=tab_bar_plus menubar=menu modal=popup resizex=child_resize\n");
	printf("         smallbutton=small_button invisiblebutton=invisible_button selectionbutton=selection_button\n");
	printf("         borderedbutton=bordered_button arrowbutton=arrow_button\n");
	printf("         colored=text_colored textdisabled=text_disabled wrapped=text_wrapped bullet=bullet_text\n");
	printf("         separatortext=separator_text radiogroup=radio_group\n");
	printf("         inputhint=text_input_hint inputsel=text_input_selection\n");
	printf("         inputml=text_input_multiline inputro=text_input_readonly\n");
	printf("         sliderint=slider_int sliderf3=slider_float3 drag=drag_float\n");
	printf("         dragint=drag_int dragf3=drag_float3\n");
	printf("         menuopen=menu_open submenu=menu_submenu menupopup=menu_popup\n");
	printf("         contextmenu=popup_menu savecontent=modal_save\n");
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
	/* Clear leftover disabled on the node + label child from a prior frame. */
	(void)ui->checkbox_set_disabled(host->ctx, node, 0);
	(void)ui->radio_set_disabled(host->ctx, node, 0);
	(void)ui->text_input_set_disabled(host->ctx, node, 0);
	(void)ui->slider_set_disabled(host->ctx, node, 0);
	(void)ui->slider_set_text_input(host->ctx, node, 0);
	(void)ui->set_disabled(host->ctx, node, 0);
	switch (st) {
	case SANDBOX_WS_DEFAULT:
		return 0;
	case SANDBOX_WS_HOVERED:
		return ui->node_set_state(host->ctx, node, (u32)SK_UI_STATE_HOVER);
	case SANDBOX_WS_PRESSED:
		return ui->node_set_state(host->ctx, node, (u32)SK_UI_STATE_ACTIVE);
	case SANDBOX_WS_DISABLED:
		(void)ui->checkbox_set_disabled(host->ctx, node, 1);
		(void)ui->radio_set_disabled(host->ctx, node, 1);
		(void)ui->text_input_set_disabled(host->ctx, node, 1);
		(void)ui->slider_set_disabled(host->ctx, node, 1);
		(void)ui->set_disabled(host->ctx, node, 1);
		return ui->node_set_state(host->ctx, node, (u32)SK_UI_STATE_DISABLED);
	case SANDBOX_WS_FOCUSED:
		if (ui->focus_set(host->ctx, node) == 0) {
			return 0;
		}
		return ui->node_set_state(host->ctx, node, (u32)SK_UI_STATE_FOCUSED);
	case SANDBOX_WS_CHECKED:
		(void)ui->checkbox_set_mixed(host->ctx, node, 0);
		(void)ui->checkbox_set_checked(host->ctx, node, 1);
		(void)ui->radio_set_checked(host->ctx, node, 1);
		return 0;
	case SANDBOX_WS_MIXED:
		(void)ui->checkbox_set_mixed(host->ctx, node, 1);
		return 0;
	case SANDBOX_WS_MIN:
	case SANDBOX_WS_MID:
	case SANDBOX_WS_MAX:
	case SANDBOX_WS_DRAGGING:
	case SANDBOX_WS_TEXT_ENTRY: {
		f32 vmin = 0.0f;
		f32 vmax = 1.0f;
		f32 mid;
		sk_ui_node_t leaf = node;
		sk_ui_node_t child = SK_UI_NODE_INVALID;
		sk_ui_prop_value_t pv;
		if (ui->slider_component(host->ctx, node, 0, &child) == 0 && sk_ui_node_is_valid(child)) {
			leaf = child;
		}
		if (ui->node_get_prop(host->ctx, leaf, "min", &pv) == 0 && pv.type == SK_UI_PROP_F32) {
			vmin = pv.data.f32_value;
		}
		if (ui->node_get_prop(host->ctx, leaf, "max", &pv) == 0 && pv.type == SK_UI_PROP_F32) {
			vmax = pv.data.f32_value;
		}
		mid = vmin + (vmax - vmin) * 0.5f;
		/* Write the leaf only so vector siblings keep independent values. */
		if (st == SANDBOX_WS_MIN) {
			(void)ui->slider_set_value(host->ctx, leaf, vmin);
		} else if (st == SANDBOX_WS_MAX) {
			(void)ui->slider_set_value(host->ctx, leaf, vmax);
		} else {
			(void)ui->slider_set_value(host->ctx, leaf, mid);
		}
		if (st == SANDBOX_WS_DRAGGING) {
			return ui->node_set_state(host->ctx, leaf, (u32)SK_UI_STATE_ACTIVE);
		}
		if (st == SANDBOX_WS_TEXT_ENTRY) {
			return ui->slider_set_text_input(host->ctx, leaf, 1);
		}
		return 0;
	}
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
			fprintf(stderr, "sk-sandbox: unknown --state '%s' (default|hovered|pressed|disabled|focused|checked|mixed|min|mid|max|dragging|text_entry|all)\n", state_filter);
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
	if (d->build == sandbox_widget_build_separator) {
		const sk_ui_api_t* ui = host->ui;
		sk_ui_rect_t r;
		sk_ui_node_t q = ui->query_by_test_id(host->ctx, SK_UI_NODE_INVALID, "review-matrix");
		if (ui->node_get_abs_rect(host->ctx, q, &r, NULL) == 0) {
			printf("DBG2 matrix x=%d y=%d w=%d h=%d\n", (int)r.x, (int)r.y, (int)r.width, (int)r.height);
		}
		q = ui->query_by_test_id(host->ctx, SK_UI_NODE_INVALID, "review-tb-autoscroll");
		if (ui->node_get_abs_rect(host->ctx, q, &r, NULL) == 0) {
			printf("DBG2 autoscroll x=%d y=%d w=%d h=%d\n", (int)r.x, (int)r.y, (int)r.width, (int)r.height);
		}
		q = ui->query_by_test_id(host->ctx, SK_UI_NODE_INVALID, "review-toolbar");
		if (ui->node_get_abs_rect(host->ctx, q, &r, NULL) == 0) {
			printf("DBG2 toolbar x=%d y=%d w=%d h=%d\n", (int)r.x, (int)r.y, (int)r.width, (int)r.height);
		}
		q = ui->query_by_test_id(host->ctx, SK_UI_NODE_INVALID, "review-sep-menubar");
		if (ui->node_get_abs_rect(host->ctx, q, &r, NULL) == 0) {
			printf("DBG2 menubar x=%d y=%d w=%d h=%d\n", (int)r.x, (int)r.y, (int)r.width, (int)r.height);
		}
		{
			const_chr_t ids[] = {"review-tb-play", "review-tb-stop",  "review-tb-pause", "review-tb-clear",
								 "review-tb-sep1", "review-tb-group", "review-tb-sep2",	 "review-tb-collapse"};
			u32 k;
			for (k = 0; k < sizeof(ids) / sizeof(ids[0]); ++k) {
				sk_ui_node_t nq = ui->query_by_test_id(host->ctx, SK_UI_NODE_INVALID, ids[k]);
				if (ui->node_get_abs_rect(host->ctx, nq, &r, NULL) == 0) {
					printf("DBG2 %s x=%d y=%d w=%d h=%d\n", ids[k], (int)r.x, (int)r.y, (int)r.width, (int)r.height);
				}
			}
		}
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
