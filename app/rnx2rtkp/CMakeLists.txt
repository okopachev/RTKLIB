add_executable(rnx2rtkp rnx2rtkp.c)
target_link_libraries(rnx2rtkp ${PROJECT_SHARED_LIBRARY} -lm -lrt)

install(TARGETS rnx2rtkp DESTINATION bin)

