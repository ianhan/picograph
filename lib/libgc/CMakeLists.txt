if (PICO_SDK)
    add_library(libgc INTERFACE)
    target_sources(libgc INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/gc.c
        ${CMAKE_CURRENT_LIST_DIR}/gcattr.c
        ${CMAKE_CURRENT_LIST_DIR}/gcrender.c
        ${CMAKE_CURRENT_LIST_DIR}/gcsw.c
        ${CMAKE_CURRENT_LIST_DIR}/gcdlo.c)
    target_include_directories(libgc INTERFACE ${CMAKE_CURRENT_LIST_DIR})
endif()
