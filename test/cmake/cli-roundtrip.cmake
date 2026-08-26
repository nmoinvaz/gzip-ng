# Drive the gzip-ng binary through a compress and decompress roundtrip in place.
file(REMOVE_RECURSE ${WORKDIR})
file(MAKE_DIRECTORY ${WORKDIR})
string(REPEAT "the quick brown fox jumps over the lazy dog 0123456789 " 20000 DATA)
file(WRITE ${WORKDIR}/data.txt "${DATA}")
file(WRITE ${WORKDIR}/data.orig "${DATA}")

execute_process(COMMAND ${EXE} ${WORKDIR}/data.txt RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "compress failed")
endif()
if(EXISTS ${WORKDIR}/data.txt)
    message(FATAL_ERROR "compress kept the original without -k")
endif()

execute_process(COMMAND ${EXE} -d ${WORKDIR}/data.txt.gz RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "decompress failed")
endif()
if(EXISTS ${WORKDIR}/data.txt.gz)
    message(FATAL_ERROR "decompress kept the input")
endif()

execute_process(COMMAND ${CMAKE_COMMAND} -E compare_files ${WORKDIR}/data.txt ${WORKDIR}/data.orig
                RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "roundtrip changed the data")
endif()
