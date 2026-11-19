find_package(Qt6 REQUIRED COMPONENTS Core)

add_library(ng_core STATIC
    src/ScriptFacade.cpp
    src/IngestPipeline.cpp
    src/IngestOrchestrator.cpp
    src/AppContext.cpp
    src/InteractionRegistry.cpp
    src/SettingsStore.cpp
    src/WorkspaceDataConfig.cpp
    include/ScriptFacade.h
    include/IngestOrchestrator.h
    include/SettingsStore.h
    include/WorkspaceDataConfig.h
    include/IngestPipeline.h
    include/AppContext.h
    include/InteractionRegistry.h
)

target_include_directories(ng_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(ng_core PUBLIC Qt6::Core ng_kernel ng_foundation ng_dataset ng_registry ng_intent ng_uischema ng_parser)
set_target_properties(ng_core PROPERTIES
    AUTOMOC ON
    CXX_STANDARD 17
)
