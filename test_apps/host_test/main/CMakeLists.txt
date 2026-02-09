
set(reqs unity uffs)

if(NOT IDF_TARGET STREQUAL "linux")
    list(APPEND reqs driver)
endif()

idf_component_register(SRCS "test_host_main.c"
                    INCLUDE_DIRS "."
                    REQUIRES ${reqs})
