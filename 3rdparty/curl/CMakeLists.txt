# CURL

if(USE_SYSTEM_CURL)
	message(STATUS "RPCS3: using shared libcurl")
	find_package(CURL REQUIRED)
	add_library(3rdparty_libcurl INTERFACE)
	target_link_libraries(3rdparty_libcurl INTERFACE CURL::libcurl)
else()
	message(STATUS "RPCS3: building libcurl + wolfssl submodules")
	set(BUILD_CURL_EXE OFF CACHE INTERNAL "")
	set(BUILD_STATIC_CURL OFF CACHE INTERNAL "")
	set(BUILD_STATIC_LIBS ON CACHE INTERNAL "")
	set(BUILD_SHARED_LIBS OFF CACHE INTERNAL "")
	find_package(WolfSSL REQUIRED)
	set(CURL_USE_WOLFSSL ON CACHE INTERNAL "")
	set(CURL_USE_OPENSSL OFF CACHE INTERNAL "")
	set(HTTP_ONLY ON CACHE INTERNAL "")
	set(USE_LIBIDN2 OFF CACHE INTERNAL "") # Disabled because MacOS CI doesn't work otherwise
	set(CURL_CA_PATH "none" CACHE INTERNAL "")
	set(CURL_DISABLE_INSTALL ON CACHE INTERNAL "")
	if(WIN32)
		set(ENABLE_UNICODE ON CACHE INTERNAL "")
	endif()
	set(CURL_USE_LIBSSH2 OFF CACHE INTERNAL "")
	set(CURL_USE_LIBPSL OFF CACHE INTERNAL "")
	set(BUILD_TESTING OFF CACHE INTERNAL "")
	set(BUILD_EXAMPLES OFF CACHE INTERNAL "")
	if(RPCS3_FRONTEND STREQUAL "IOS" AND CMAKE_SYSTEM_NAME STREQUAL "Darwin")
		# Match the mobile HTTP profile and keep this core relocatable. Native
		# package discovery must not add optional Homebrew runtime dylibs.
		set(CURL_BROTLI OFF CACHE STRING "" FORCE)
		set(CURL_ZSTD OFF CACHE STRING "" FORCE)
		set(USE_NGHTTP2 OFF CACHE BOOL "" FORCE)
	endif()

	add_subdirectory(curl EXCLUDE_FROM_ALL)

	add_library(3rdparty_libcurl INTERFACE)
	target_link_libraries(3rdparty_libcurl INTERFACE libcurl_static)

endif()
