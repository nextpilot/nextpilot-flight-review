# ng_intent：意图与 Job 的纯契约库。
add_library(ng_intent STATIC
    src/Intent.cpp
    include/Intent.h
    include/IIntentExecutor.h
    include/JobRecorder.h
    include/OpDispatch.h
)

target_include_directories(ng_intent PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(ng_intent PUBLIC Qt6::Core ng_foundation ng_registry)
set_target_properties(ng_intent PROPERTIES CXX_STANDARD 17)
