{
	"permutations" : [
		{
			"name" : "Prefilter",
			"stages" : [
				{
					"entryPoint" : "CSPrefilterDepths16x16",
					"stage" : "Compute",
				  "macros" : [
						"VA_COMPILED_AS_SHADER_CODE=1"
					]
				}
			]
		}
	]
}