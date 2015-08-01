project (lvfs-bits)

# Project header
project_library_header_default ()

# 3rdparty
list (APPEND ${PROJECT_NAME}_LIBS brolly)
list (APPEND ${PROJECT_NAME}_LIBS efc)
list (APPEND ${PROJECT_NAME}_LIBS lvfs)
list (APPEND ${PROJECT_NAME}_LIBS torrent-rasterbar)

find_package (CURL REQUIRED)
include_directories (${CURL_INCLUDE_DIRS})
list (APPEND ${PROJECT_NAME}_LIBS ${CURL_LIBRARIES})

# Sources
add_subdirectory (src)

# Target - lvfs-bits
add_library (lvfs-bits SHARED ${${PROJECT_NAME}_SOURCES})
target_link_libraries (lvfs-bits ${${PROJECT_NAME}_LIBS})

# Version
build_version_file (lvfs-bits 0.0.1)

# Documentation
add_documentation ("LVFS-BitS" 0.0.1 "LVFS Plugin for BitTorrent protocol")

# Install rules
install_target (lvfs-bits)
