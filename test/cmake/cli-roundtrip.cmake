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

# -c writes to stdout and keeps the file.
file(WRITE ${WORKDIR}/c.txt "${DATA}")
execute_process(COMMAND ${EXE} -c ${WORKDIR}/c.txt OUTPUT_FILE ${WORKDIR}/c.txt.gz RESULT_VARIABLE rc)
if(NOT rc EQUAL 0 OR NOT EXISTS ${WORKDIR}/c.txt)
    message(FATAL_ERROR "-c compress failed or removed the file")
endif()
execute_process(COMMAND ${EXE} -d -c ${WORKDIR}/c.txt.gz OUTPUT_FILE ${WORKDIR}/c.out RESULT_VARIABLE rc)
if(NOT rc EQUAL 0 OR NOT EXISTS ${WORKDIR}/c.txt.gz)
    message(FATAL_ERROR "-d -c failed or removed the file")
endif()
execute_process(COMMAND ${CMAKE_COMMAND} -E compare_files ${WORKDIR}/c.txt ${WORKDIR}/c.out
                RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "-c roundtrip changed the data")
endif()

# -k keeps the input on both directions.
file(WRITE ${WORKDIR}/k.txt "${DATA}")
execute_process(COMMAND ${EXE} -k ${WORKDIR}/k.txt RESULT_VARIABLE rc)
if(NOT rc EQUAL 0 OR NOT EXISTS ${WORKDIR}/k.txt OR NOT EXISTS ${WORKDIR}/k.txt.gz)
    message(FATAL_ERROR "-k compress failed to keep")
endif()
file(REMOVE ${WORKDIR}/k.txt)
execute_process(COMMAND ${EXE} -d -k ${WORKDIR}/k.txt.gz RESULT_VARIABLE rc)
if(NOT rc EQUAL 0 OR NOT EXISTS ${WORKDIR}/k.txt.gz)
    message(FATAL_ERROR "-d -k failed to keep")
endif()
execute_process(COMMAND ${CMAKE_COMMAND} -E compare_files ${WORKDIR}/k.txt ${WORKDIR}/data.orig
                RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "-k roundtrip changed the data")
endif()

# Levels roundtrip, stored and fast.
foreach(level 0 1)
    file(WRITE ${WORKDIR}/l.txt "${DATA}")
    execute_process(COMMAND ${EXE} -${level} ${WORKDIR}/l.txt RESULT_VARIABLE rc)
    execute_process(COMMAND ${EXE} -d ${WORKDIR}/l.txt.gz RESULT_VARIABLE rc2)
    execute_process(COMMAND ${CMAKE_COMMAND} -E compare_files ${WORKDIR}/l.txt ${WORKDIR}/data.orig
                    RESULT_VARIABLE rc3)
    if(NOT rc EQUAL 0 OR NOT rc2 EQUAL 0 OR NOT rc3 EQUAL 0)
        message(FATAL_ERROR "level -${level} roundtrip failed")
    endif()
endforeach()
