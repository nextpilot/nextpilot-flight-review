# ps_intent：意图与 Job 的纯契约库。
add_library(ps_intent STATIC
    src/Intent.cpp
    include/Intent.h
    include/IIntentExecutor.h
    include/JobRecorder.h
    include/OpDispatch.h
)

target_include_directories(ps_intent PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(ps_intent PUBLIC Qt6::Core ps_foundation ps_registry)
set_target_properties(ps_intent PROPERTIES CXX_STANDARD 17)
