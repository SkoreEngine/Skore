find_program(SK_DOTNET_EXECUTABLE NAMES dotnet REQUIRED)

set(SK_MANAGED_PROJECT ${CMAKE_CURRENT_SOURCE_DIR}/SkoreEngine.csproj)
set(SK_MANAGED_OUTPUT_DIR ${CMAKE_BINARY_DIR}/bin/managed)
set(SK_MANAGED_INTERMEDIATE_DIR ${CMAKE_BINARY_DIR}/dotnet/SkoreEngine/obj)
set(SK_MANAGED_ASSEMBLY ${SK_MANAGED_OUTPUT_DIR}/SkoreEngine.dll)

file(GLOB_RECURSE SK_MANAGED_SOURCES CONFIGURE_DEPENDS
		${CMAKE_CURRENT_SOURCE_DIR}/*.cs)

add_custom_target(SkoreManaged ALL
		COMMAND ${SK_DOTNET_EXECUTABLE} build ${SK_MANAGED_PROJECT}
				--configuration $<IF:$<CONFIG:Debug>,Debug,Release>
				--output ${SK_MANAGED_OUTPUT_DIR}
				-p:BaseIntermediateOutputPath=${SK_MANAGED_INTERMEDIATE_DIR}/
				-nologo
		BYPRODUCTS ${SK_MANAGED_ASSEMBLY}
		WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
		COMMENT "Building managed assembly SkoreEngine.dll (dotnet)"
		VERBATIM)

set(SK_GENERATED_BINDINGS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/Generated)

add_custom_target(SkoreGenerateBindings
		COMMAND $<TARGET_FILE:SkorePlayer> --generate-bindings ${SK_GENERATED_BINDINGS_DIR}
		WORKING_DIRECTORY $<TARGET_FILE_DIR:SkorePlayer>
		COMMENT "Generating C# bindings (SkorePlayer --generate-bindings -> Generated/)"
		USES_TERMINAL
		VERBATIM)

add_dependencies(SkoreGenerateBindings SkorePlayer)
