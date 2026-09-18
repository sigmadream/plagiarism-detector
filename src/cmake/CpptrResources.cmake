function(cpptr_add_resources_target target_name)
    set(options)
    set(one_value_args SOURCE_DIR BINARY_DIR)
    set(multi_value_args)
    cmake_parse_arguments(CPPTR_RES "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

    if(NOT CPPTR_RES_SOURCE_DIR)
        message(FATAL_ERROR "cpptr_add_resources_target requires SOURCE_DIR")
    endif()

    if(NOT CPPTR_RES_BINARY_DIR)
        message(FATAL_ERROR "cpptr_add_resources_target requires BINARY_DIR")
    endif()

    add_custom_target(${target_name}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CPPTR_RES_BINARY_DIR}
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CPPTR_RES_SOURCE_DIR} ${CPPTR_RES_BINARY_DIR}
    )

    install(DIRECTORY ${CPPTR_RES_SOURCE_DIR}/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/cpptr/resources
        COMPONENT runtime
    )
endfunction()
