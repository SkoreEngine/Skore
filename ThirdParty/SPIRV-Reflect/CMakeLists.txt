add_library(SPIRV-Reflect STATIC
				Source/spirv_reflect.c
				Source/spirv_reflect.h
)
target_include_directories(SPIRV-Reflect PUBLIC Source)