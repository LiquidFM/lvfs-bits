project (lvfs-bits)

# Project header
project_cxx_library_header_default ()

# 3rdparty
list (APPEND ${PROJECT_NAME}_LIBS brolly)
list (APPEND ${PROJECT_NAME}_LIBS efc)
list (APPEND ${PROJECT_NAME}_LIBS lvfs)

# Sources
add_subdirectory (src)

# Target - lvfs-bits
add_library (lvfs-bits SHARED ${${PROJECT_NAME}_SOURCES})
target_link_libraries (lvfs-bits ${${PROJECT_NAME}_LIBS})

# Version
build_version_file (lvfs-bits 0.0.1)

# Documentation
add_documentation ("LVFS-BitS" 0.0.1 "LVFS Plugin for bit-torrent protocol")

# Install rules
install_target (lvfs-bits)
