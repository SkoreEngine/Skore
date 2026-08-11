#include "ui_capture_harness.h"
#include "ui.h"
#include "test.h"
#include <stdio.h>
#include <string.h>

typedef struct uii_ctx_t {
	const sk_ui_api_t* ui;
} uii_ctx_t;

static i32 scene_empty(sk_ui_capture_scene_t* scene, void* user) {
	((uii_ctx_t*)user)->ui = scene->ui;
	return 0;
}

int main(void) {
	int i;
	for (i = 0; i < 3; ++i) {
		sk_ui_capture_harness_params_t params;
		sk_ui_cpu_image_t img;
		uii_ctx_t uictx;
		int rc;
		memset(&uictx, 0, sizeof(uictx));
		memset(&params, 0, sizeof(params));
		params.scene_name = "repro_cycles";
		params.width = 96u;
		params.height = 96u;
		params.clear_color_set = 1;
		params.clear_color = sk_ui_rgba(0.0f, 0.0f, 0.0f, 1.0f);
		params.time_seconds = 0.0;
		rc = sk_ui_capture_harness_capture(&params, scene_empty, &uictx, &img);
		fprintf(stderr, "cycle %d rc=%d ui=%p\n", i, rc, (const void*)uictx.ui);
		if (rc == SK_UI_CAPTURE_HARNESS_RC_OK) {
			fprintf(stderr, "  px0=%02x%02x%02x%02x\n", img.pixels[0], img.pixels[1], img.pixels[2], img.pixels[3]);
			sk_ui_capture_harness_image_free(&img);
		}
	}
	fprintf(stderr, "ALL CYCLES OK\n");
	return 0;
}
