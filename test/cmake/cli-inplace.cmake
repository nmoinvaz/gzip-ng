# cli-inplace.cmake -- In-place file semantics that the stream harness does not cover, the input
#   is removed unless -k, -c keeps it, -T copies through untouched, and threads do not change
#   the compressed bytes.

file(REMOVE_RECURSE ${WORKDIR})
file(MAKE_DIRECTORY ${WORKDIR})
string(REPEAT "the quick brown fox jumps over the lazy dog 0123456789 " 20000 DATA)
file(WRITE ${WORKDIR}/data.orig "${DATA}")

# In place, the input is consumed in both directions.
file(WRITE ${WORKDIR}/data.txt "${DATA}")
execute_process(COMMAND ${EXE} ${WORKDIR}/data.txt RESULT_VARIABLE rc)
if(NOT rc EQUAL 0 OR EXISTS ${WORKDIR}/data.txt OR NOT EXISTS ${WORKDIR}/data.txt.gz)
    message(FATAL_ERROR "compress did not consume the input")
endif()
execute_process(COMMAND ${EXE} -d ${WORKDIR}/data.txt.gz RESULT_VARIABLE rc)
if(NOT rc EQUAL 0 OR EXISTS ${WORKDIR}/data.txt.gz)
    message(FATAL_ERROR "decompress did not consume the input")
endif()
execute_process(COMMAND ${CMAKE_COMMAND} -E compare_files ${WORKDIR}/data.txt ${WORKDIR}/data.orig
                RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "roundtrip changed the data")
endif()

# -k keeps the input, -c keeps it too.
file(WRITE ${WORKDIR}/k.txt "${DATA}")
execute_process(COMMAND ${EXE} -k ${WORKDIR}/k.txt RESULT_VARIABLE rc)
if(NOT rc EQUAL 0 OR NOT EXISTS ${WORKDIR}/k.txt OR NOT EXISTS ${WORKDIR}/k.txt.gz)
    message(FATAL_ERROR "-k did not keep the input")
endif()
execute_process(COMMAND ${EXE} -c ${WORKDIR}/k.txt OUTPUT_FILE ${WORKDIR}/c.gz RESULT_VARIABLE rc)
if(NOT rc EQUAL 0 OR NOT EXISTS ${WORKDIR}/k.txt)
    message(FATAL_ERROR "-c did not keep the input")
endif()

# -T copies through untouched and decompression passes it back through.
execute_process(COMMAND ${EXE} -T -c ${WORKDIR}/k.txt OUTPUT_FILE ${WORKDIR}/t.raw RESULT_VARIABLE rc)
execute_process(COMMAND ${CMAKE_COMMAND} -E compare_files ${WORKDIR}/k.txt ${WORKDIR}/t.raw
                RESULT_VARIABLE rc2)
if(NOT rc EQUAL 0 OR NOT rc2 EQUAL 0)
    message(FATAL_ERROR "-T did not copy through")
endif()

# The thread count must not change the compressed bytes.
execute_process(COMMAND ${EXE} -b 64K -p 1 -c ${WORKDIR}/k.txt OUTPUT_FILE ${WORKDIR}/p1.gz RESULT_VARIABLE rc)
execute_process(COMMAND ${EXE} -b 64K -p 3 -c ${WORKDIR}/k.txt OUTPUT_FILE ${WORKDIR}/p3.gz RESULT_VARIABLE rc2)
execute_process(COMMAND ${CMAKE_COMMAND} -E compare_files ${WORKDIR}/p1.gz ${WORKDIR}/p3.gz
                RESULT_VARIABLE rc3)
if(NOT rc EQUAL 0 OR NOT rc2 EQUAL 0 OR NOT rc3 EQUAL 0)
    message(FATAL_ERROR "thread count changed the compressed bytes")
endif()

# Without -f an existing output is refused with a warning, -f overwrites it.
file(WRITE ${WORKDIR}/o.txt "${DATA}")
file(WRITE ${WORKDIR}/o.txt.gz "junk")
execute_process(COMMAND ${EXE} -k ${WORKDIR}/o.txt RESULT_VARIABLE rc)
file(READ ${WORKDIR}/o.txt.gz EXISTING)
if(rc EQUAL 0 OR NOT EXISTING STREQUAL "junk")
    message(FATAL_ERROR "existing output was overwritten without -f")
endif()
execute_process(COMMAND ${EXE} -f -k ${WORKDIR}/o.txt RESULT_VARIABLE rc)
execute_process(COMMAND ${EXE} -d -c ${WORKDIR}/o.txt.gz OUTPUT_FILE ${WORKDIR}/o.out RESULT_VARIABLE rc2)
execute_process(COMMAND ${CMAKE_COMMAND} -E compare_files ${WORKDIR}/o.txt ${WORKDIR}/o.out
                RESULT_VARIABLE rc3)
if(NOT rc EQUAL 0 OR NOT rc2 EQUAL 0 OR NOT rc3 EQUAL 0)
    message(FATAL_ERROR "-f overwrite roundtrip failed")
endif()

# -r walks directories, compressing then restoring, a bare directory is only a warning.
file(MAKE_DIRECTORY ${WORKDIR}/tree/sub)
file(WRITE ${WORKDIR}/tree/a.txt "${DATA}")
file(WRITE ${WORKDIR}/tree/sub/b.txt "${DATA}")
execute_process(COMMAND ${EXE} ${WORKDIR}/tree RESULT_VARIABLE rc)
if(rc EQUAL 0)
    message(FATAL_ERROR "directory without -r should warn")
endif()
execute_process(COMMAND ${EXE} -r ${WORKDIR}/tree RESULT_VARIABLE rc)
if(NOT rc EQUAL 0 OR NOT EXISTS ${WORKDIR}/tree/a.txt.gz OR NOT EXISTS ${WORKDIR}/tree/sub/b.txt.gz
   OR EXISTS ${WORKDIR}/tree/a.txt)
    message(FATAL_ERROR "-r compress walk failed")
endif()
execute_process(COMMAND ${EXE} -d -r ${WORKDIR}/tree RESULT_VARIABLE rc)
execute_process(COMMAND ${CMAKE_COMMAND} -E compare_files ${WORKDIR}/tree/sub/b.txt ${WORKDIR}/data.orig
                RESULT_VARIABLE rc2)
if(NOT rc EQUAL 0 OR NOT rc2 EQUAL 0 OR EXISTS ${WORKDIR}/tree/a.txt.gz)
    message(FATAL_ERROR "-d -r walk failed")
endif()

# The stored name comes back with -N, and -n stores nothing.
file(WRITE ${WORKDIR}/named.txt "${DATA}")
execute_process(COMMAND ${EXE} -k ${WORKDIR}/named.txt RESULT_VARIABLE rc)
file(RENAME ${WORKDIR}/named.txt.gz ${WORKDIR}/moved.gz)
file(REMOVE ${WORKDIR}/named.txt)
execute_process(COMMAND ${EXE} -d -N ${WORKDIR}/moved.gz RESULT_VARIABLE rc2)
if(NOT rc EQUAL 0 OR NOT rc2 EQUAL 0 OR NOT EXISTS ${WORKDIR}/named.txt)
    message(FATAL_ERROR "-N did not restore the stored name")
endif()
file(WRITE ${WORKDIR}/anon.txt "${DATA}")
execute_process(COMMAND ${EXE} -n -k ${WORKDIR}/anon.txt RESULT_VARIABLE rc)
file(RENAME ${WORKDIR}/anon.txt.gz ${WORKDIR}/plain.gz)
execute_process(COMMAND ${EXE} -d -N -k ${WORKDIR}/plain.gz RESULT_VARIABLE rc2)
if(NOT rc EQUAL 0 OR NOT rc2 EQUAL 0 OR NOT EXISTS ${WORKDIR}/plain)
    message(FATAL_ERROR "-n left a name in the header")
endif()

# -l reports the trailer size and the derived name.
string(LENGTH "${DATA}" DATA_LEN)
file(WRITE ${WORKDIR}/listed.txt "${DATA}")
execute_process(COMMAND ${EXE} -k ${WORKDIR}/listed.txt RESULT_VARIABLE rc)
execute_process(COMMAND ${EXE} -l ${WORKDIR}/listed.txt.gz OUTPUT_VARIABLE listing RESULT_VARIABLE rc2)
string(FIND "${listing}" "${DATA_LEN}" size_at)
string(FIND "${listing}" "listed.txt" name_at)
if(NOT rc EQUAL 0 OR NOT rc2 EQUAL 0 OR size_at EQUAL -1 OR name_at EQUAL -1)
    message(FATAL_ERROR "-l listing was wrong: ${listing}")
endif()

# -t checks integrity without writing anything.
execute_process(COMMAND ${EXE} -t ${WORKDIR}/listed.txt.gz RESULT_VARIABLE rc)
file(WRITE ${WORKDIR}/junk.gz "this is not gzip data at all")
execute_process(COMMAND ${EXE} -t ${WORKDIR}/junk.gz RESULT_VARIABLE rc2 ERROR_QUIET)
if(NOT rc EQUAL 0 OR rc2 EQUAL 0 OR NOT EXISTS ${WORKDIR}/listed.txt.gz)
    message(FATAL_ERROR "-t verdicts were wrong")
endif()

# --synchronous roundtrips with the data intact.
file(WRITE ${WORKDIR}/sync.txt "${DATA}")
execute_process(COMMAND ${EXE} --synchronous ${WORKDIR}/sync.txt RESULT_VARIABLE rc)
execute_process(COMMAND ${EXE} --synchronous -d ${WORKDIR}/sync.txt.gz RESULT_VARIABLE rc2)
execute_process(COMMAND ${CMAKE_COMMAND} -E compare_files ${WORKDIR}/sync.txt ${WORKDIR}/data.orig
                RESULT_VARIABLE rc3)
if(NOT rc EQUAL 0 OR NOT rc2 EQUAL 0 OR NOT rc3 EQUAL 0)
    message(FATAL_ERROR "--synchronous roundtrip failed")
endif()

# -c stores the name too, gzip does, so the header carries it either way.
file(WRITE ${WORKDIR}/keptname.txt "${DATA}")
execute_process(COMMAND ${EXE} -c ${WORKDIR}/keptname.txt OUTPUT_FILE ${WORKDIR}/fromc.gz RESULT_VARIABLE rc)
file(READ ${WORKDIR}/fromc.gz HEADBYTES HEX LIMIT 64)
string(FIND "${HEADBYTES}" "6b6570746e616d652e747874" name_at)
if(NOT rc EQUAL 0 OR name_at EQUAL -1)
    message(FATAL_ERROR "-c did not store the file name")
endif()
