###################################
## Setup all of the test executables in a for loop
set(TEST_LIST
    test_berendsen_integrator
    test_bondtable_bond_force
    test_external_periodic
    test_fire_energy_minimizer
    test_cosinesq_angle_force
    test_harmonic_angle_force
    test_harmonic_bond_force
    test_harmonic_dihedral_force
    test_harmonic_improper_force
    test_MolecularForceCompute
    test_neighborlist
    test_opls_dihedral_force
    test_pppm_force
    test_table_angle_force
    test_table_dihedral_force
    test_walldata
    test_zero_momentum_updater
    )

if (NOT HOOMD_SKIP_LONG_TESTS)
    # put the longest tests last
set(TEST_LIST ${TEST_LIST}
    test_dpd_integrator
    test_npt_mtk_integrator
    test_nvt_mtk_integrator
    test_nve_integrator)
endif()

if(ENABLE_MPI)
    MACRO(ADD_TO_MPI_TESTS _KEY _VALUE)
    SET("NProc_${_KEY}" "${_VALUE}")
    SET(MPI_TEST_LIST ${MPI_TEST_LIST} ${_KEY})
    ENDMACRO(ADD_TO_MPI_TESTS)

    # define every test together with the number of processors

    ADD_TO_MPI_TESTS(test_communication 8)
    ADD_TO_MPI_TESTS(test_communicator_grid 8)
endif()

foreach (CUR_TEST ${TEST_LIST} ${MPI_TEST_LIST})
    # add and link the unit test executable
    if(ENABLE_HIP AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${CUR_TEST}.cu)
        set(_cuda_sources ${CUR_TEST}.cu)
    else()
        set(_cuda_sources "")
    endif()

    add_executable(${CUR_TEST} EXCLUDE_FROM_ALL ${CUR_TEST}.cc ${_cuda_sources})
    target_include_directories(${CUR_TEST} PRIVATE ${PYTHON_INCLUDE_DIR})

    add_dependencies(test_all ${CUR_TEST})

    if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
        # these options are needed to avoid linker errors with GCC
        set(additional_link_options "-Wl,--allow-shlib-undefined -Wl,--no-as-needed")
    endif()
    target_link_libraries(${CUR_TEST} _md ${additional_link_options} ${PYTHON_LIBRARIES})

    fix_cudart_rpath(${CUR_TEST})

endforeach (CUR_TEST)

# add non-MPI tests to test list first
foreach (CUR_TEST ${TEST_LIST})
    # add it to the unit test list
    if (ENABLE_MPI)
        add_test(NAME ${CUR_TEST} COMMAND ${MPIEXEC} ${MPIEXEC_NUMPROC_FLAG} 1 ${MPIEXEC_POSTFLAGS} $<TARGET_FILE:${CUR_TEST}>)
    else()
        add_test(NAME ${CUR_TEST} COMMAND $<TARGET_FILE:${CUR_TEST}>)
    endif()
endforeach(CUR_TEST)

# add MPI tests
foreach (CUR_TEST ${MPI_TEST_LIST})
    # add it to the unit test list
    # add mpi- prefix to distinguish these tests
    set(MPI_TEST_NAME mpi-${CUR_TEST})

    add_test(NAME ${MPI_TEST_NAME} COMMAND
             ${MPIEXEC} ${MPIEXEC_NUMPROC_FLAG}
             ${NProc_${CUR_TEST}} ${MPIEXEC_POSTFLAGS}
             $<TARGET_FILE:${CUR_TEST}>)
endforeach(CUR_TEST)
