find_package(Qt6 REQUIRED COMPONENTS Core)

add_library(ng_core STATIC
    src/Dataset.cpp
    src/DataRepository.cpp
    src/Intent.cpp
    include/Result.h
    include/Dataset.h
    include/DataRepository.h
    include/Events.h
    include/Intent.h
)

target_include_directories(ng_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(ng_core PUBLIC Qt6::Core)
set_target_properties(ng_core PROPERTIES
    AUTOMOC ON
    CXX_STANDARD 17
)
