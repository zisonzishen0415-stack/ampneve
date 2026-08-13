add_executable(ampsim_render ampsim_render.c)
target_include_directories(ampsim_render PRIVATE ${CMAKE_SOURCE_DIR}/core)
target_link_libraries(ampsim_render PRIVATE ampneve_core)
ampneve_warnings(ampsim_render)
