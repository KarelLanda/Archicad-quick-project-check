cmake_minimum_required (VERSION 3.16)

# Find Python 3
find_package (Python3 REQUIRED COMPONENTS Interpreter)
message (STATUS "Using Python: ${Python3_EXECUTABLE} (version ${Python3_VERSION})")

function (SetCompilerOptions target)
	target_compile_features (${target} PUBLIC cxx_std_20)
	target_compile_options (${target} PUBLIC "$<$<CONFIG:Debug>:-DDEBUG>")
	if (WIN32)
		target_compile_options (${target} PUBLIC /W3 /wd4996 /Zc:wchar_t- /utf-8)
	else ()
		target_compile_options (${target} PUBLIC -Wall -Werror -fvisibility=hidden
			-Wno-multichar
			-Wno-ctor-dtor-privacy
			-Wno-invalid-offsetof
			-Wno-ignored-qualifiers
			-Wno-reorder
			-Wno-overloaded-virtual
			-Wno-unused-parameter
			-Wno-missing-field-initializers
			-Wno-unknown-pragmas
			-Wno-misleading-indentation
			-Wno-missing-braces
			-Wno-unused-private-field
			-Wno-return-std-move
			-Wno-unused-value
			-Wno-switch
			-Wno-deprecated
			-Wno-shorten-64-to-32)
	endif ()
endfunction ()

function (AddGSModuleIncludeFolder target folder)
	target_include_directories (${target} PUBLIC "${folder}")
endfunction ()

function (AddGSModuleLinkLibrary target library)
	if (WIN32)
		file (GLOB libs "${library}/Win/*.lib")
		foreach (lib ${libs})
			target_link_libraries (${target} ${lib})
		endforeach ()
	else ()
		target_link_libraries (${target} ${library})
	endif ()
endfunction ()

set_property (GLOBAL PROPERTY USE_FOLDERS ON)
set (CMAKE_SUPPRESS_REGENERATION 1)
set (CMAKE_CONFIGURATION_TYPES Debug;Release;RelWithDebInfo)

if (WIN32)
	set (CMAKE_MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
else ()
	set (CMAKE_OSX_DEPLOYMENT_TARGET 15.1)
	if (CMAKE_GENERATOR STREQUAL Xcode)
		set (CMAKE_OSX_ARCHITECTURES "$(ARCHS_STANDARD)" CACHE INTERNAL "" FORCE)
		set (CMAKE_XCODE_ATTRIBUTE_ONLY_ACTIVE_ARCH "NO")
	endif ()
endif ()

# Default DevKit path (can be overridden via -DAC_API_DEVKIT_DIR=...)
if (WIN32)
	set (AC_API_DEVKIT_DIR "C:/Graphisoft/API.Development.Kit.WIN.29.3100" CACHE PATH "API DevKit directory.")
else ()
	set (AC_API_DEVKIT_DIR "/Users/karellanda/Library/CloudStorage/GoogleDrive-landakarel66@gmail.com/Můj disk/Archicad/Graphisoft-API/AC29/API.Development.Kit.MAC.29.3100" CACHE PATH "API DevKit directory.")
endif ()

set (AC_ADDON_NAME "KontrolaModelu" CACHE STRING "Add-On name.")
set (AC_ADDON_LANGUAGE "INT" CACHE STRING "Add-On language code.")

set (ACAPINC_FILE_LOCATION "")
if (EXISTS "${AC_API_DEVKIT_DIR}/Support/Inc/ACAPinc.h")
	set (ACAPINC_FILE_LOCATION "${AC_API_DEVKIT_DIR}/Support/Inc/ACAPinc.h")
	set (AC_DEVKIT_SUPPORT_DIR "${AC_API_DEVKIT_DIR}/Support")
elseif (EXISTS "${AC_API_DEVKIT_DIR}/Inc/ACAPinc.h")
	set (ACAPINC_FILE_LOCATION "${AC_API_DEVKIT_DIR}/Inc/ACAPinc.h")
	set (AC_DEVKIT_SUPPORT_DIR "${AC_API_DEVKIT_DIR}")
else ()
	message (FATAL_ERROR "Failed to detect Archicad version, please check AC_API_DEVKIT_DIR: ${AC_API_DEVKIT_DIR}")
endif ()

file (READ ${ACAPINC_FILE_LOCATION} ACAPIncContent)
string (REGEX MATCHALL "#define[ \t]+ServerMainVers_([0-9][0-9])" VersionList ${ACAPIncContent})
set (AC_MAIN_VERSION ${CMAKE_MATCH_1})
message (STATUS "Detected Archicad version: ${AC_MAIN_VERSION} from ${ACAPINC_FILE_LOCATION}")

if (WIN32)
	add_definitions (-DUNICODE -D_UNICODE -D_ITERATOR_DEBUG_LEVEL=0 -DGS_WIN)
else ()
	add_definitions (-Dmacintosh=1 -DGS_MAC)
endif ()
add_definitions (-DACExtension)

project (${AC_ADDON_NAME})

set (AddOnSourcesFolder ./Src)
set (AddOnResourcesFolder .)
set (ResourceObjectsDir ${CMAKE_BINARY_DIR}/ResourceObjects)

file (GLOB AddOnImageFiles
	${AddOnResourcesFolder}/RFIX/Images/*.svg
)

if (WIN32)
	file (GLOB AddOnResourceFiles
		${AddOnResourcesFolder}/R${AC_ADDON_LANGUAGE}/*.grc
		${AddOnResourcesFolder}/RFIX/*.grc
		${AddOnResourcesFolder}/RFIX.win/*.rc2
	)
else ()
	file (GLOB AddOnResourceFiles
		${AddOnResourcesFolder}/R${AC_ADDON_LANGUAGE}/*.grc
		${AddOnResourcesFolder}/RFIX/*.grc
		${AddOnResourcesFolder}/RFIX.mac/*.plist
	)
endif ()

add_custom_target (
	AddOnResources ALL
	DEPENDS "${ResourceObjectsDir}/AddOnResources.stamp"
	SOURCES ${AddOnResourceFiles} ${AddOnImageFiles}
)

get_filename_component (AddOnSourcesFolderAbsolute "${CMAKE_CURRENT_LIST_DIR}/${AddOnSourcesFolder}" ABSOLUTE)
get_filename_component (AddOnResourcesFolderAbsolute "${CMAKE_CURRENT_LIST_DIR}/${AddOnResourcesFolder}" ABSOLUTE)
set (LocalCompileResourcesScript "${CMAKE_CURRENT_LIST_DIR}/Tools/CompileResourcesAC.py")

if (WIN32)
	add_custom_command (
		OUTPUT "${ResourceObjectsDir}/AddOnResources.stamp"
		DEPENDS ${AddOnResourceFiles} ${AddOnImageFiles} "${LocalCompileResourcesScript}"
		COMMENT "Compiling resources..."
		COMMAND ${CMAKE_COMMAND} -E make_directory "${ResourceObjectsDir}"
		COMMAND ${Python3_EXECUTABLE} "${LocalCompileResourcesScript}" "${AC_ADDON_LANGUAGE}" "${AC_API_DEVKIT_DIR}" "${AddOnSourcesFolderAbsolute}" "${AddOnResourcesFolderAbsolute}" "${ResourceObjectsDir}" "${ResourceObjectsDir}/${AC_ADDON_NAME}.res"
		COMMAND ${CMAKE_COMMAND} -E touch "${ResourceObjectsDir}/AddOnResources.stamp"
	)
else ()
	add_custom_command (
		OUTPUT "${ResourceObjectsDir}/AddOnResources.stamp"
		DEPENDS ${AddOnResourceFiles} ${AddOnImageFiles} "${LocalCompileResourcesScript}"
		COMMENT "Compiling resources..."
		COMMAND ${CMAKE_COMMAND} -E make_directory "${ResourceObjectsDir}"
		COMMAND ${Python3_EXECUTABLE} "${LocalCompileResourcesScript}" "${AC_ADDON_LANGUAGE}" "${AC_DEVKIT_SUPPORT_DIR}" "${AddOnSourcesFolderAbsolute}" "${AddOnResourcesFolderAbsolute}" "${ResourceObjectsDir}" "${CMAKE_BINARY_DIR}/$<CONFIG>/${AC_ADDON_NAME}.bundle/Contents/Resources"
		COMMAND ${CMAKE_COMMAND} -E copy "${AC_DEVKIT_SUPPORT_DIR}/Inc/PkgInfo" "${CMAKE_BINARY_DIR}/$<CONFIG>/${AC_ADDON_NAME}.bundle/Contents/PkgInfo"
		COMMAND ${CMAKE_COMMAND} -E touch "${ResourceObjectsDir}/AddOnResources.stamp"
	)
endif ()

file (GLOB AddOnHeaderFiles
	${AddOnSourcesFolder}/*.h
	${AddOnSourcesFolder}/*.hpp
)

file (GLOB AddOnSourceFiles
	${AddOnSourcesFolder}/*.c
	${AddOnSourcesFolder}/*.cpp
	${AddOnSourcesFolder}/*.mm
)
file (GLOB AllCFiles
	${AddOnSourcesFolder}/*.c
)
set_source_files_properties(${AllCFiles} PROPERTIES LANGUAGE CXX)

set (AddOnFiles ${AddOnHeaderFiles} ${AddOnSourceFiles})

if (WIN32)
	add_library (AddOn SHARED ${AddOnFiles})
	set_target_properties (AddOn PROPERTIES OUTPUT_NAME ${AC_ADDON_NAME})
	set_target_properties (AddOn PROPERTIES SUFFIX ".apx")
	target_link_options (AddOn PUBLIC "${ResourceObjectsDir}/${AC_ADDON_NAME}.res")
	target_link_options (AddOn PUBLIC /export:GetExportedFuncAddrs,@1 /export:SetImportedFuncAddrs,@2)
else ()
	add_library (AddOn MODULE ${AddOnFiles})
	set_target_properties (AddOn PROPERTIES OUTPUT_NAME ${AC_ADDON_NAME})
	set_target_properties (AddOn PROPERTIES BUNDLE TRUE)
	set_target_properties (AddOn PROPERTIES MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_LIST_DIR}/${AddOnResourcesFolder}/RFIX.mac/Info.plist")
	set_target_properties (AddOn PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>")
endif ()

target_include_directories (AddOn PUBLIC
	${AddOnSourcesFolder}
	${AC_DEVKIT_SUPPORT_DIR}/Inc
)

if (WIN32)
	target_link_libraries (AddOn
		"${AC_DEVKIT_SUPPORT_DIR}/Lib/ACAP_STAT.lib"
	)
else ()
	find_library (CocoaFramework Cocoa)
	target_link_libraries (AddOn
		"${AC_DEVKIT_SUPPORT_DIR}/Lib/libACAP_STAT.a"
		${CocoaFramework}
	)
endif ()

SetCompilerOptions (AddOn)
add_dependencies (AddOn AddOnResources)

get_filename_component (APIDevKitModulesDir "${AC_DEVKIT_SUPPORT_DIR}/Modules" ABSOLUTE)
file (GLOB GSIncludeFolders ${APIDevKitModulesDir}/*)
foreach (includeFolder ${GSIncludeFolders})
	get_filename_component (folderName "${includeFolder}" NAME)
	if (NOT "${folderName}" STREQUAL ".DS_Store")
		AddGSModuleIncludeFolder (AddOn ${includeFolder})
	endif ()
endforeach ()

if (WIN32)
	set (APIDevKitLinkLibDir "${APIDevKitModulesDir}")
else ()
	get_filename_component (APIDevKitLinkLibDir "${AC_DEVKIT_SUPPORT_DIR}/Frameworks" ABSOLUTE)
endif ()

file (GLOB GSLinkLibFolders ${APIDevKitLinkLibDir}/*)
foreach (linkLibFolder ${GSLinkLibFolders})
	get_filename_component (folderName "${linkLibFolder}" NAME)
	if (NOT "${folderName}" STREQUAL ".DS_Store")
		if (IS_DIRECTORY "${linkLibFolder}")
			AddGSModuleLinkLibrary (AddOn ${linkLibFolder})
		endif ()
	endif ()
endforeach ()

get_filename_component (new_hpp "${APIDevKitModulesDir}/GSRoot/GSNew.hpp" REALPATH)
get_filename_component (malloc_hpp "${APIDevKitModulesDir}/GSRoot/GSMalloc.hpp" REALPATH)

if (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
	target_compile_options (AddOn PRIVATE
		"SHELL:/FI \"${new_hpp}\""
		"SHELL:/FI \"${malloc_hpp}\""
	)
elseif (CMAKE_CXX_COMPILER_ID MATCHES "Clang$")
	target_compile_options (AddOn PRIVATE
		"SHELL:-include \"${new_hpp}\""
		"SHELL:-include \"${malloc_hpp}\""
	)
endif ()

option (SIGN_BUNDLE "Sign bundle with Developer ID" OFF)

if (NOT WIN32)
	set (CODESIGN_IDENTITY "Developer ID Application: Karel Landa (4T62PM76AY)")
	set (TEST_ADDONS_DIR "/Users/karellanda/Library/CloudStorage/GoogleDrive-landakarel66@gmail.com/Můj disk/Archicad/Doplnky Archicad/Testovane doplnky/AC${AC_MAIN_VERSION}")
	set (TEST_ADDONS_ROOT_DIR "/Users/karellanda/Library/CloudStorage/GoogleDrive-landakarel66@gmail.com/Můj disk/Archicad/Doplnky Archicad/Testovane doplnky")
	if (SIGN_BUNDLE)
		add_custom_command (TARGET AddOn POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E echo "Signing bundle with Developer ID certificate..."
			COMMAND rm -rf "/tmp/${AC_ADDON_NAME}_sign_${AC_MAIN_VERSION}"
			COMMAND mkdir -p "/tmp/${AC_ADDON_NAME}_sign_${AC_MAIN_VERSION}"
			COMMAND cp -R "${CMAKE_BINARY_DIR}/$<CONFIG>/${AC_ADDON_NAME}.bundle" "/tmp/${AC_ADDON_NAME}_sign_${AC_MAIN_VERSION}/"
			COMMAND chmod -R u+w "/tmp/${AC_ADDON_NAME}_sign_${AC_MAIN_VERSION}/${AC_ADDON_NAME}.bundle"
			COMMAND xattr -cr "/tmp/${AC_ADDON_NAME}_sign_${AC_MAIN_VERSION}/${AC_ADDON_NAME}.bundle"
			COMMAND codesign --force --deep --options runtime --timestamp --sign "${CODESIGN_IDENTITY}" "/tmp/${AC_ADDON_NAME}_sign_${AC_MAIN_VERSION}/${AC_ADDON_NAME}.bundle"
			COMMAND codesign --verify --deep --strict --verbose=2 "/tmp/${AC_ADDON_NAME}_sign_${AC_MAIN_VERSION}/${AC_ADDON_NAME}.bundle"
			COMMAND rm -rf "${CMAKE_BINARY_DIR}/$<CONFIG>/${AC_ADDON_NAME}.bundle"
			COMMAND cp -R "/tmp/${AC_ADDON_NAME}_sign_${AC_MAIN_VERSION}/${AC_ADDON_NAME}.bundle" "${CMAKE_BINARY_DIR}/$<CONFIG>/"
			COMMAND ${CMAKE_COMMAND} -E echo "Copying signed bundle to Testovane doplnky folder..."
			COMMAND mkdir -p "${TEST_ADDONS_DIR}"
			COMMAND rm -rf "${TEST_ADDONS_DIR}/${AC_ADDON_NAME}.bundle"
			COMMAND cp -R "/tmp/${AC_ADDON_NAME}_sign_${AC_MAIN_VERSION}/${AC_ADDON_NAME}.bundle" "${TEST_ADDONS_DIR}/"
			COMMAND rm -rf "${TEST_ADDONS_DIR}/${AC_ADDON_NAME}-AC${AC_MAIN_VERSION}.bundle"
			COMMAND cp -R "/tmp/${AC_ADDON_NAME}_sign_${AC_MAIN_VERSION}/${AC_ADDON_NAME}.bundle" "${TEST_ADDONS_DIR}/${AC_ADDON_NAME}-AC${AC_MAIN_VERSION}.bundle"
			COMMAND rm -rf "${TEST_ADDONS_ROOT_DIR}/${AC_ADDON_NAME}-AC${AC_MAIN_VERSION}.bundle"
			COMMAND cp -R "/tmp/${AC_ADDON_NAME}_sign_${AC_MAIN_VERSION}/${AC_ADDON_NAME}.bundle" "${TEST_ADDONS_ROOT_DIR}/${AC_ADDON_NAME}-AC${AC_MAIN_VERSION}.bundle"
			COMMAND rm -rf "/tmp/${AC_ADDON_NAME}_sign_${AC_MAIN_VERSION}"
			VERBATIM
		)
	else ()
		add_custom_command (TARGET AddOn POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E echo "Copying bundle to Testovane doplnky folder (unsigned dev build)..."
			COMMAND rm -rf "${CMAKE_BINARY_DIR}/$<CONFIG>/${AC_ADDON_NAME}.bundle/Contents/_CodeSignature"
			COMMAND mkdir -p "${TEST_ADDONS_DIR}"
			COMMAND rm -rf "${TEST_ADDONS_DIR}/${AC_ADDON_NAME}.bundle"
			COMMAND cp -R "${CMAKE_BINARY_DIR}/$<CONFIG>/${AC_ADDON_NAME}.bundle" "${TEST_ADDONS_DIR}/"
			COMMAND rm -rf "${TEST_ADDONS_DIR}/${AC_ADDON_NAME}-AC${AC_MAIN_VERSION}.bundle"
			COMMAND cp -R "${CMAKE_BINARY_DIR}/$<CONFIG>/${AC_ADDON_NAME}.bundle" "${TEST_ADDONS_DIR}/${AC_ADDON_NAME}-AC${AC_MAIN_VERSION}.bundle"
			COMMAND rm -rf "${TEST_ADDONS_ROOT_DIR}/${AC_ADDON_NAME}-AC${AC_MAIN_VERSION}.bundle"
			COMMAND cp -R "${CMAKE_BINARY_DIR}/$<CONFIG>/${AC_ADDON_NAME}.bundle" "${TEST_ADDONS_ROOT_DIR}/${AC_ADDON_NAME}-AC${AC_MAIN_VERSION}.bundle"
			COMMAND rm -rf "${TEST_ADDONS_ROOT_DIR}/${AC_ADDON_NAME}.bundle"
			COMMAND cp -R "${CMAKE_BINARY_DIR}/$<CONFIG>/${AC_ADDON_NAME}.bundle" "${TEST_ADDONS_ROOT_DIR}/${AC_ADDON_NAME}.bundle"
			VERBATIM
		)
	endif ()
endif ()
