add_executable(peg_gen
    src/main.cpp
    src/grammar_parser.cpp
    src/module_resolver.cpp
    src/codegen.cpp
)

target_link_libraries(peg_gen PRIVATE
    logos_hermes   # peg_gen uses Hermes internally for grammar AST
    logos_core
)

target_include_directories(peg_gen PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    src
)

# Install templates and bundled grammars alongside the executable.
install(DIRECTORY templates/ DESTINATION share/peg_gen/templates)
install(DIRECTORY grammars/  DESTINATION share/peg_gen/grammars)
