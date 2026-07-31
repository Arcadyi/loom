# Console tool: no QML engine, no window, so QtCore is all it needs on top of
# the loom library itself.
qt_add_executable(loomstyle main.cpp)
target_link_libraries(loomstyle PRIVATE loom)
loom_set_target_defaults(loomstyle)
set_target_properties(loomstyle PROPERTIES WIN32_EXECUTABLE FALSE MACOSX_BUNDLE FALSE)

install(TARGETS loomstyle RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
