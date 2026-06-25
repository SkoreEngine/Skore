option(SK_BUILD_MCP "Build the Skore MCP server (requires Node.js / npm)" ON)

if (NOT SK_BUILD_MCP)
	return()
endif()

find_program(SK_NPM_EXECUTABLE NAMES npm npm.cmd)

if (NOT SK_NPM_EXECUTABLE)
	message(WARNING "SK_BUILD_MCP is ON but npm was not found; skipping the Skore MCP build. Install Node.js or configure with -DSK_BUILD_MCP=OFF.")
	return()
endif()

set(SK_MCP_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(SK_MCP_INSTALL_STAMP ${CMAKE_CURRENT_BINARY_DIR}/npm-install.stamp)
set(SK_MCP_OUTPUT ${SK_MCP_DIR}/dist/skore-mcp.mjs)

file(GLOB SK_MCP_SOURCES CONFIGURE_DEPENDS ${SK_MCP_DIR}/src/*.ts)

add_custom_command(
		OUTPUT ${SK_MCP_INSTALL_STAMP}
		COMMAND ${SK_NPM_EXECUTABLE} ci --no-audit --no-fund
		COMMAND ${CMAKE_COMMAND} -E touch ${SK_MCP_INSTALL_STAMP}
		WORKING_DIRECTORY ${SK_MCP_DIR}
		DEPENDS ${SK_MCP_DIR}/package.json ${SK_MCP_DIR}/package-lock.json
		COMMENT "Installing Skore MCP npm dependencies (npm ci)"
		VERBATIM)

add_custom_command(
		OUTPUT ${SK_MCP_OUTPUT}
		COMMAND ${SK_NPM_EXECUTABLE} run build
		WORKING_DIRECTORY ${SK_MCP_DIR}
		DEPENDS ${SK_MCP_INSTALL_STAMP} ${SK_MCP_SOURCES} ${SK_MCP_DIR}/tsconfig.json ${SK_MCP_DIR}/esbuild.config.mjs
		COMMENT "Building Skore MCP (typecheck + esbuild bundle)"
		VERBATIM)

if (CMAKE_RUNTIME_OUTPUT_DIRECTORY)
	set(SK_MCP_DEPLOY_DIR ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/Mcp)
else()
	set(SK_MCP_DEPLOY_DIR ${CMAKE_BINARY_DIR}/bin/Mcp)
endif()

set(SK_MCP_DEPLOY_OUTPUT ${SK_MCP_DEPLOY_DIR}/skore-mcp.mjs)

add_custom_command(
		OUTPUT ${SK_MCP_DEPLOY_OUTPUT}
		COMMAND ${CMAKE_COMMAND} -E make_directory ${SK_MCP_DEPLOY_DIR}
		COMMAND ${CMAKE_COMMAND} -E copy ${SK_MCP_OUTPUT} ${SK_MCP_DEPLOY_OUTPUT}
		COMMAND ${CMAKE_COMMAND} -E copy ${SK_MCP_DIR}/README.md ${SK_MCP_DEPLOY_DIR}/README.md
		DEPENDS ${SK_MCP_OUTPUT}
		COMMENT "Deploying Skore MCP (single bundled file) to ${SK_MCP_DEPLOY_DIR}"
		VERBATIM)

add_custom_target(SkoreMcp ALL DEPENDS ${SK_MCP_DEPLOY_OUTPUT})
