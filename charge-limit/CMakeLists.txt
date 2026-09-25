project(cutefish-charge-limit)
set(TARGET cutefish-charge-limit)

add_executable(${TARGET} main.cpp)
target_link_libraries(${TARGET}
        Qt6::Core
)

configure_file(
    com.cutefish.chargelimit.pkexec.policy.in
    com.cutefish.chargelimit.pkexec.policy
    @ONLY
)

install(TARGETS ${TARGET} DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/com.cutefish.chargelimit.pkexec.policy DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/polkit-1/actions/)
