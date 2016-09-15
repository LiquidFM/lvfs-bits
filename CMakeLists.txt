project (lvfs-bits)

# Project header (FIXME: "STRICT_WARNINGS:NO" because of libtorrent)
project_header_default ("POSITION_INDEPENDENT_CODE:YES" "STRICT_WARNINGS:NO")

# 3rdparty
list (APPEND ${PROJECT_NAME}_LIBS ${EFC_LIB})
list (APPEND ${PROJECT_NAME}_LIBS ${LVFS_LIB})

find_package (LibTorrent REQUIRED)
include_directories (${LIBTORRENT_INCLUDE})
list (APPEND ${PROJECT_NAME}_LIBS ${LIBTORRENT_LIBRARY})

# Sources
add_subdirectory (src)

# Target - lvfs-bits
add_library (lvfs-bits SHARED ${${PROJECT_NAME}_SOURCES})
target_link_libraries (lvfs-bits ${${PROJECT_NAME}_LIBS})

# Documentation
add_documentation (lvfs-bits 0.0.1 "LVFS Plugin for BitTorrent protocol")

# Install rules
install_target (lvfs-bits)
