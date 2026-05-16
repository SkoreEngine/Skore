file(GLOB_RECURSE MSDFGEN_SRC
				msdfgen/core/*.h
				msdfgen/core/*.hpp
				msdfgen/core/*.cpp
				msdfgen/ext/*.h
				msdfgen/ext/*.hpp
				msdfgen/ext/*.cpp
)
add_library(msdfgen STATIC ${MSDFGEN_SRC})
target_link_libraries(msdfgen PRIVATE freetype)


target_compile_definitions(msdfgen PUBLIC MSDFGEN_PUBLIC=)
target_include_directories(msdfgen PUBLIC msdfgen)


file(GLOB_RECURSE MSDF_ATLAS_GEN_SRC
				msdf-atlas-gen/*.h
				msdf-atlas-gen/*.hpp
				msdf-atlas-gen/*.cpp
				msdf-atlas-gen/*.c
)

add_library(msdf-atlas-gen STATIC ${MSDF_ATLAS_GEN_SRC})
target_compile_definitions(msdf-atlas-gen PUBLIC MSDF_ATLAS_NO_ARTERY_FONT=1)

target_include_directories(msdf-atlas-gen PUBLIC msdf-atlas-gen)


target_link_libraries(msdf-atlas-gen PUBLIC msdfgen)