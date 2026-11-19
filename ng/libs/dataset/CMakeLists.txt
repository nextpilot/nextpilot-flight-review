# ng_dataset：数据地基（Dataset 门面 + DataRepository 仓库）
#
# 只依赖 Rust kernel（ng_kernel）+ Qt Core + ng_foundation（Result.h），
# 不碰 core 任何业务类型（source / parser / intent / 飞行 / CAN）。
# 任何想用数据模型的消费者可只链本库，不必吞下整个 ng_core。
add_library(ng_dataset STATIC
    src/Dataset.cpp
    src/DataRepository.cpp
    src/SourceRegistry.cpp
    include/Dataset.h
    include/DataRepository.h
    include/Source.h
    include/DataSource.h
    include/SourceRegistry.h
)

target_include_directories(ng_dataset PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
# ng_kernel: Rust ps-abi（Dataset 的 C ABI 门）；ng_foundation: Result.h（DataRepository 返回类型）。
target_link_libraries(ng_dataset PUBLIC ng_kernel ng_foundation Qt6::Core)
set_target_properties(ng_dataset PROPERTIES AUTOMOC ON CXX_STANDARD 17)
