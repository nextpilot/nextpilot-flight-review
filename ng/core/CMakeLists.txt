find_package(Qt6 REQUIRED COMPONENTS Core)

add_library(ng_core STATIC
    src/Dataset.cpp
    src/DataRepository.cpp
    src/Intent.cpp
    src/ScriptFacade.cpp
    src/IFormatDriver.cpp
    src/ExtensionRegistry.cpp
    include/Result.h
    include/Dataset.h
    include/DataRepository.h
    include/Events.h
    include/Intent.h
    include/IIntentExecutor.h
    include/JobRecorder.h
    include/ScriptFacade.h
    include/IFormatDriver.h
    include/ExtensionRegistry.h
)

target_include_directories(ng_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(ng_core PUBLIC Qt6::Core ng_kernel)
set_target_properties(ng_core PROPERTIES
    AUTOMOC ON
    CXX_STANDARD 17
)
