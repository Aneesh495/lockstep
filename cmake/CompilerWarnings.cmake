# CompilerWarnings.cmake
# Sets up compiler warnings with warnings-as-errors

set(WARNINGS_GNU
    -Wall
    -Wextra
    -Wpedantic
    -Werror
    -Wshadow
    -Wconversion
    -Wsign-conversion
    -Wdouble-promotion
    -Wformat=2
    -Wuninitialized
    -Wold-style-cast
    -Wnull-dereference
    -Woverloaded-virtual
    -Wnon-virtual-dtor
    -Wcast-align
    -Wunused
    -Wmissing-include-dirs
    -Wno-unused-parameter
)

set(WARNINGS_CLANG
    ${WARNINGS_GNU}
    -Wdocumentation
    -Wdocumentation-unknown-command
)

function(set_target_warnings target)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(${target} PRIVATE ${WARNINGS_GNU})
    elseif(CMAKE_CXX_COMPILER_ID MATCHES ".*Clang")
        target_compile_options(${target} PRIVATE ${WARNINGS_CLANG})
    endif()
endfunction()
