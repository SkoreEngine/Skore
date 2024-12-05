{
	"permutations": [
		{
			"name" : "ResolveTemporal",
			"stages" : [
				{
					"entryPoint" : "ResolveTemporal",
					"stage" : "Compute",
					"macros" : [
						"SK_HISTORY_CONSTRAINT_MODE_VARIANCE_CLIP_CLAMP=1",
						"SK_USE_TEMPORAL_FILTERING=1",
						"SK_USE_INVERSE_LUMINANCE_FILTERING=1",
						"SK_USE_LUMINANCE_DIFFERENCE_FILTERING=1"
					]
				}
			]
		},
		{
			"name" : "UpdateHistory",
			"stages" : [
				{
					"entryPoint" : "UpdateHistory",
					"stage" : "Compute",
					"macros" : [
						"SK_APPLY_SHARPENING=1"
					]
				}
			]
		}
	]
}