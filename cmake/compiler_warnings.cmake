# from here:
#
# https://github.com/lefticus/cppbestpractices/blob/master/02-Use_the_Tools_Available.md

set(CLANG_WARNINGS
	-Wall
	-Wextra # reasonable and standard
	-Wshadow # warn the user if a variable declaration shadows one from a parent context
	"$<$<COMPILE_LANGUAGE:CXX>:-Wnon-virtual-dtor>"# warn the user if a class with virtual functions has a non-virtual destructor. This helps
					# catch hard to track down memory errors
	"$<$<COMPILE_LANGUAGE:CXX>:-Wold-style-cast>" # warn for c-style casts
	"$<$<COMPILE_LANGUAGE:CXX>:-Woverloaded-virtual>" # warn if you overload (not override) a virtual function
	-Wcast-align # warn for potential performance problem casts
	-Wunused # warn on anything being unused
	-Wpedantic # warn if non-standard C++ is used
	-Wconversion # warn on type conversions that may lose data
	-Wsign-conversion # warn on sign conversions
	-Wnull-dereference # warn if a null dereference is detected
	-Wdouble-promotion # warn if float is implicit promoted to double
	-Wformat=2 # warn on security issues around functions that format output (ie printf)
)

set(GCC_WARNINGS
	${CLANG_WARNINGS}
	-Wmisleading-indentation # warn if indentation implies blocks where blocks do not exist
	-Wduplicated-cond # warn if if / else chain has duplicated conditions
	-Wduplicated-branches # warn if if / else branches have duplicated code
	-Wlogical-op # warn about logical operations being used where bitwise were probably wanted
	-Wuseless-cast # warn if you perform a cast to the same type
)

if(CMAKE_CXX_COMPILER_ID MATCHES ".*Clang")
	set(PROJECT_WARNINGS ${CLANG_WARNINGS})
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
	set(PROJECT_WARNINGS ${GCC_WARNINGS})
else()
	message(AUTHOR_WARNING "No compiler warnings set for '${CMAKE_CXX_COMPILER_ID}' compiler.")
endif()

add_library(common_warnings INTERFACE)
target_compile_options(common_warnings INTERFACE
	-Werror
	${PROJECT_WARNINGS}
)