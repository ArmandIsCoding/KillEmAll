cmake_minimum_required(VERSION 3.28)
project(KillEmAll LANGUAGES CXX)

include(FetchContent)
set(SFML_BUILD_AUDIO ON)
set(SFML_BUILD_NETWORK OFF)
FetchContent_Declare(SFML
    GIT_REPOSITORY https://github.com/SFML/SFML.git
    GIT_TAG 3.1.0
    GIT_SHALLOW ON
    EXCLUDE_FROM_ALL
    SYSTEM
)
FetchContent_MakeAvailable(SFML)

add_executable(KillEmAll
    src/main.cpp
    src/Entity.cpp
    src/Entity.hpp
    src/Player.cpp
    src/Player.hpp
    src/Combat.cpp
    src/Combat.hpp
    src/CampaignCheckpoint.cpp
    src/CampaignCheckpoint.hpp
    src/CampaignProgress.cpp
    src/CampaignProgress.hpp
    src/HullGeometry.hpp
    src/CarrierRaid.cpp
    src/CarrierRaid.hpp
    src/CarrierRenderer.cpp
    src/CarrierRenderer.hpp
    src/CampaignScenery.cpp
    src/CampaignScenery.hpp
    src/StageBoss.cpp
    src/StageBoss.hpp
    src/StageBossRenderer.cpp
    src/StageBossRenderer.hpp
    src/BattleRenderer.cpp
    src/BattleRenderer.hpp
    src/GameAudio.cpp
    src/GameAudio.hpp
    src/Game.cpp
    src/Game.hpp
    src/Hud.cpp
    src/Hud.hpp
)
target_compile_features(KillEmAll PRIVATE cxx_std_17)
target_link_libraries(KillEmAll PRIVATE SFML::Graphics SFML::Audio)
if(WIN32)
    set_property(TARGET KillEmAll PROPERTY WIN32_EXECUTABLE "$<CONFIG:Release>")
    target_link_libraries(KillEmAll PRIVATE SFML::Main)
endif()

add_custom_target(GameAssets
    COMMAND "${CMAKE_COMMAND}" -E copy_directory_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:KillEmAll>/assets"
    COMMENT "Updating game assets"
    VERBATIM
)
add_dependencies(KillEmAll GameAssets)

set_property(TARGET KillEmAll PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:KillEmAll>")
set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT KillEmAll)
