# Get the current version control revision
message("dir is ${DIR}.")
if(EXISTS "${DIR}/.hg_archival.txt")
  file(READ "${DIR}/.hg_archival.txt" HG_ARCHIVAL)
  if(HG_ARCHIVAL MATCHES ".*node:.*")
    string(REGEX REPLACE ".*node: ([0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f]).*" "\\1" HG_NODE "${HG_ARCHIVAL}")
    if(HG_ARCHIVAL MATCHES ".*local:.*")
      string(REGEX REPLACE ".*local: ([0-9][0-9]*).*" "\\1" HG_LOCAL "${HG_ARCHIVAL}")
      set(MERCURIAL_ID "${HG_NODE} ${HG_LOCAL}")
    else()
      set(MERCURIAL_ID "${HG_NODE}")
    endif()
    if(HG_ARCHIVAL MATCHES ".*branch:.*")
      string(REGEX REPLACE ".*branch: ([-._A-Za-z0-9]*).*" "\\1" HG_BRANCH "${HG_ARCHIVAL}")
      set(MERCURIAL_BRANCH "${HG_BRANCH}")
    endif()
  else()
    message(WARNING "Failed to find mercurial ID")
    set(MERCURIAL_ID "Unknown")
  endif()
elseif(EXISTS "${DIR}/.hg")
  find_package(Hg)
  if(HG_FOUND)
    message("hg found: ${HG_EXECUTABLE}")
    execute_process(COMMAND "${HG_EXECUTABLE}" "id" "-i" WORKING_DIRECTORY "${DIR}" RESULT_VARIABLE HG_RETURN_CODE
      OUTPUT_VARIABLE HG_OUPUT_RES OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(HG_RETURN_CODE EQUAL 0 AND HG_OUPUT_RES)
      set(MERCURIAL_ID "${HG_OUPUT_RES}")
    else()
      message(WARNING "Failed to find mercurial ID")
      set(MERCURIAL_ID "Unknown")
    endif()
    execute_process(COMMAND "${HG_EXECUTABLE}" "id" "-b" WORKING_DIRECTORY "${DIR}" RESULT_VARIABLE HG_RETURN_CODE2
      OUTPUT_VARIABLE HG_OUPUT_RES2 OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(HG_RETURN_CODE2 EQUAL 0 AND HG_OUPUT_RES2)
      set(MERCURIAL_BRANCH "${HG_OUPUT_RES2}")
    else()
      message(WARNING "Failed to find mercurial branch")
    endif()
  else()
    message(WARNING "Failed to find mercurial")
    set(MERCURIAL_ID "Unknown")
  endif()
elseif(EXISTS "${DIR}/.git")
  find_package(Git)
  if(GIT_FOUND)
    message("git found: ${GIT_EXECUTABLE}")
    execute_process(COMMAND "${GIT_EXECUTABLE}" "rev-parse" "--short" "HEAD" WORKING_DIRECTORY "${DIR}"
      RESULT_VARIABLE GIT_RETURN_CODE OUTPUT_VARIABLE GIT_OUPUT_RES OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(GIT_RETURN_CODE EQUAL 0 AND GIT_OUPUT_RES)
      set(MERCURIAL_ID "${GIT_OUPUT_RES}")
    else()
      message(WARNING "Failed to find git ID")
      set(MERCURIAL_ID "Unknown")
    endif()
  else()
    message(WARNING "Failed to find git")
    set(MERCURIAL_ID "Unknown")
  endif()
else()
  set(MERCURIAL_ID "Unknown")
endif()

message("Configure ${SRC} with ${MERCURIAL_ID}.")
configure_file(${SRC} ${DST} @ONLY)
