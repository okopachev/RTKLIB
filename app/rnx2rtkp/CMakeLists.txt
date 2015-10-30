add_executable(rnx2rtkp rnx2rtkp.c)

if(WIN32)
  target_link_libraries(rnx2rtkp ${PROJECT_SHARED_LIBRARY})
else(WIN32)
  target_link_libraries(rnx2rtkp ${PROJECT_SHARED_LIBRARY} -lm -lrt)
endif(WIN32)

install(TARGETS rnx2rtkp DESTINATION bin)

