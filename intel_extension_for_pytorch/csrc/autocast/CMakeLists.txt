FILE(GLOB _CPU_SRCS *.cpp)
LIST(APPEND DPCPP_AUTOCAST_SRCS ${_CPU_SRCS})

# Pass to parent
set(DPCPP_AUTOCAST_SRCS ${DPCPP_AUTOCAST_SRCS} PARENT_SCOPE)
