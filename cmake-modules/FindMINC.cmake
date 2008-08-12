# FindMINC.cmake module

FIND_PACKAGE(NETCDF)
FIND_PACKAGE(HDF5)


FIND_PATH(MINC_INCLUDE_DIR minc2.h /usr/include /usr/local/include /usr/local/bic/include)
FIND_LIBRARY(MINC_LIBRARY NAMES libminc2 PATHS /usr/lib /usr/local/lib /usr/local/bic/lib)


IF (MINC_INCLUDE_DIR AND MINC_LIBRARY)
   SET(MINC_FOUND TRUE)
   
   LIST(APPEND MINC_INCLUDE_DIR ${NETCDF_INCLUDE_DIR} ${HDF5_INCLUDE_DIR})
   LIST(APPEND MINC_LIBRARY ${NETCDF_LIBRARY} ${HDF5_LIBRARY})
   
ENDIF (MINC_INCLUDE_DIR AND MINC_LIBRARY)


IF (MINC_FOUND)
   IF (NOT Minc_FIND_QUIETLY)
      MESSAGE(STATUS "Found MINC: ${MINC_LIBRARY}")
   ENDIF (NOT Minc_FIND_QUIETLY)
ELSE (MINC_FOUND)
   IF (Minc_FIND_REQUIRED)
      MESSAGE(FATAL_ERROR "Cound not find MINC")
   ENDIF (Minc_FIND_REQUIRED)
ENDIF (MINC_FOUND)