if (STATIC_BUILD OR NOT USE_SYSTEM_ZLIB)
  SET(ZLIB_SOURCES_DIR ${CMAKE_BINARY_DIR}/zlib-1.2.7)
  SET(ZLIB_URL "http://www.montefiore.ulg.ac.be/~jodogne/Orthanc/ThirdPartyDownloads/zlib-1.2.7.tar.gz")
  SET(ZLIB_MD5 "60df6a37c56e7c1366cca812414f7b85")

  DownloadPackage(${ZLIB_MD5} ${ZLIB_URL} "${ZLIB_SOURCES_DIR}")

  include_directories(
    ${ZLIB_SOURCES_DIR}
    )

  list(APPEND ZLIB_SOURCES 
    ${ZLIB_SOURCES_DIR}/adler32.c
    ${ZLIB_SOURCES_DIR}/compress.c
    ${ZLIB_SOURCES_DIR}/crc32.c 
    ${ZLIB_SOURCES_DIR}/deflate.c 
    ${ZLIB_SOURCES_DIR}/gzclose.c 
    ${ZLIB_SOURCES_DIR}/gzlib.c 
    ${ZLIB_SOURCES_DIR}/gzread.c 
    ${ZLIB_SOURCES_DIR}/gzwrite.c 
    ${ZLIB_SOURCES_DIR}/infback.c 
    ${ZLIB_SOURCES_DIR}/inffast.c 
    ${ZLIB_SOURCES_DIR}/inflate.c 
    ${ZLIB_SOURCES_DIR}/inftrees.c 
    ${ZLIB_SOURCES_DIR}/trees.c 
    ${ZLIB_SOURCES_DIR}/uncompr.c 
    ${ZLIB_SOURCES_DIR}/zutil.c
    )

  source_group(ThirdParty\\zlib REGULAR_EXPRESSION ${ZLIB_SOURCES_DIR}/.*)

else()
  include(FindZLIB)
  include_directories(${ZLIB_INCLUDE_DIRS})
  link_libraries(${ZLIB_LIBRARIES})
endif()
