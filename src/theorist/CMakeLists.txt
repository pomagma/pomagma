include_directories(${CMAKE_SOURCE_DIR}/include)

add_library(pomagma_theorist SHARED
	hypothesize.cpp
	consistency.cpp
	conjecture_equal.cpp
	conjecture_diverge.cpp
)
set(POMAGMA_THEORIST_LIBS
	pomagma_theorist
	pomagma_macrostructure
	pomagma_platform_sequential
	pomagma_language
	${POMAGMA_SEQUENTIAL_LIBS}
)

add_executable(try_prove_nless try_prove_nless_main.cpp)
target_link_libraries(try_prove_nless ${POMAGMA_THEORIST_LIBS})
