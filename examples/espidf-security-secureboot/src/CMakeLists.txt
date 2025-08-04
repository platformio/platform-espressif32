idf_component_register(SRCS "secure-boot-app.c")
target_compile_options(${COMPONENT_LIB} PRIVATE "-Wno-format")