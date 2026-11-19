find_package(Qt6 REQUIRED COMPONENTS Core)

add_library(ng_core STATIC
    src/ScriptFacade.cpp
    src/IFormatDriver.cpp
    src/ExtensionRegistry.cpp
    src/FormatDomain.cpp
    src/ContributionKind.cpp
    src/UiSlotRegistry.cpp
    src/PanelFactoryRegistry.cpp
    src/BuiltinPackRegistry.cpp
    src/BuiltinContributions.cpp
    src/DataSourceRepository.cpp
    src/IngestPipeline.cpp
    src/IngestOrchestrator.cpp
    src/IByteSource.cpp
    src/AppContext.cpp
    src/SettingsStore.cpp
    src/WorkspaceDataConfig.cpp
    src/ParserRegistry.cpp
    src/IMessageParser.cpp
    src/RecordingFormat.cpp
    src/RecordingBackend.cpp
    src/PropertySchema.cpp
    src/Selection.cpp
    src/Transform.cpp
    include/Events.h
    include/ScriptFacade.h
    include/IFormatDriver.h
    include/IByteSource.h
    include/IngestOrchestrator.h
    include/ExtensionRegistry.h
    include/FormatDomain.h
    include/ContributionKind.h
    include/IToolContribution.h
    include/IToolPack.h
    include/IUiContribution.h
    include/IScriptContribution.h
    include/IDataContribution.h
    include/IExampleContribution.h
    include/IDocContribution.h
    include/UiSlotRegistry.h
    include/PanelFactoryRegistry.h
    include/BuiltinPackRegistry.h
    include/BuiltinContributions.h
    include/DataSource.h
    include/SettingsStore.h
    include/WorkspaceDataConfig.h
    include/DataSourceRepository.h
    include/IngestPipeline.h
    include/AppContext.h
    include/ParserRegistry.h
    include/ps_parser_spi.h
    include/DecodedFrame.h
    include/IMessageParser.h
    include/ps_message_parser_spi.h
    include/RecordingFormat.h
    include/RecordingBackend.h
    include/PropertySchema.h
    include/Selection.h
    include/Transform.h
)

target_include_directories(ng_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(ng_core PUBLIC Qt6::Core ng_kernel ng_foundation ng_dataset ng_registry ng_intent)
set_target_properties(ng_core PROPERTIES
    AUTOMOC ON
    CXX_STANDARD 17
)
