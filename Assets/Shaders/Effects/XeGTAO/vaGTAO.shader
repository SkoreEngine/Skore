{
	"permutations" : [
		{
			"name" : "Prefilter",
			"stages" : [
				{
					"entryPoint" : "CSPrefilterDepths16x16",
					"stage" : "Compute",
					"macros" : [
						"VA_COMPILED_AS_SHADER_CODE=1",
						"XE_GTAO_USE_HALF_FLOAT_PRECISION=0",
						"XE_GTAO_USE_DEFAULT_CONSTANTS=1",
						"XE_GTAO_FP32_DEPTHS=1"
					]
				}
			]
		},
		{
			"name" : "CSGTAOUltra",
			"stages" : [
				{
					"entryPoint" : "CSGTAOUltra",
					"stage" : "Compute",
					"macros" : [
						"VA_COMPILED_AS_SHADER_CODE=1",
						"XE_GTAO_USE_HALF_FLOAT_PRECISION=0",
						"XE_GTAO_USE_DEFAULT_CONSTANTS=1",
						"XE_GTAO_FP32_DEPTHS=1"
					]
				}
			]
		},
		{
			"name" : "CSDenoisePass",
			"stages" : [
				{
					"entryPoint" : "CSDenoisePass",
					"stage" : "Compute",
					"macros" : [
						"VA_COMPILED_AS_SHADER_CODE=1",
						"XE_GTAO_USE_HALF_FLOAT_PRECISION=0",
						"XE_GTAO_USE_DEFAULT_CONSTANTS=1",
						"XE_GTAO_FP32_DEPTHS=1"
					]
				}
			]
		},
		{
			"name": "CSDenoiseLastPass",
			"stages": [
				{
					"entryPoint": "CSDenoiseLastPass",
					"stage": "Compute",
					"macros": [
						"VA_COMPILED_AS_SHADER_CODE=1",
						"XE_GTAO_USE_HALF_FLOAT_PRECISION=0",
						"XE_GTAO_USE_DEFAULT_CONSTANTS=1",
						"XE_GTAO_FP32_DEPTHS=1"
					]
				}
			]
		}
	]
}