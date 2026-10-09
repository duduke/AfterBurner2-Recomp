cmake_minimum_required(VERSION 3.20)
project(afterburner2-recomp CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

find_package(SDL2 REQUIRED)
find_package(Threads REQUIRED)

add_executable(afterburner2
    src/main/main.cpp
    src/main/romloader.cpp
    src/main/roms.cpp
    src/main/hwvideo/hwroad.cpp
    src/main/hwvideo/hwsprites.cpp
    src/main/hwvideo/hwtiles.cpp
    src/main/hwvideo/hwpalette.cpp
    src/main/hwvideo/screen.cpp
    src/main/hwaudio/arcade_audio.cpp
    src/third_party/ymfm/ymfm_opm.cpp
    src/main/hwaudio/xboard_sound_cpu.cpp
    src/main/hwaudio/soundchip.cpp
    src/main/hwaudio/segapcm.cpp
    src/main/hwaudio/ym2151.cpp
    src/main/engine/ohud.cpp
    src/main/engine/osprites.cpp
    src/main/engine/omaininput.cpp
    src/main/engine/omainplayer.cpp
    src/main/engine/omaindamage.cpp src/main/engine/omainsequence.cpp
    src/main/engine/omainworld.cpp src/main/engine/omainweapons.cpp
    src/main/engine/omainobjects.cpp
    src/main/engine/omainhost.cpp
    src/main/engine/omainsound.cpp
    src/main/engine/omainpalette.cpp
    src/main/engine/omainframe.cpp
    src/main/engine/omainsprites.cpp
    src/main/engine/osubsprites.cpp
    src/main/engine/olevelobjs.cpp
    src/main/engine/oengine.cpp
    src/main/engine/subbus.cpp
    src/main/engine/mainbus.cpp
    src/main/engine/omainadc.cpp
    src/main/engine/omainirqservice.cpp src/main/engine/omainirqcallee.cpp
    src/main/engine/oscenery.cpp
    src/main/engine/ostagesetup.cpp
    src/main/engine/osubroad.cpp
    src/main/engine/osubirq.cpp src/main/engine/osubprogress.cpp src/main/engine/osubpublication.cpp
)

target_include_directories(afterburner2 PRIVATE src/main ${SDL2_INCLUDE_DIRS})
target_link_libraries(afterburner2 PRIVATE ${SDL2_LIBRARIES} Threads::Threads)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(afterburner2 PRIVATE -Wall -Wextra)
endif()

include(CTest)
if(BUILD_TESTING)
    find_package(Threads REQUIRED)
    add_executable(game_controls_regression tests/game_controls_regression.cpp)
    target_include_directories(game_controls_regression PRIVATE src/main)
    add_test(NAME game_controls_regression COMMAND game_controls_regression)
    add_executable(threejs_scene_regression tests/threejs_scene_regression.cpp src/main/hwvideo/hwsprites.cpp)
    target_include_directories(threejs_scene_regression PRIVATE src/main)
    add_test(NAME threejs_scene_regression COMMAND threejs_scene_regression)
    add_executable(cpu_continuation_regression tests/cpu_continuation_regression.cpp)
    target_include_directories(cpu_continuation_regression PRIVATE src/main)
    target_link_libraries(cpu_continuation_regression PRIVATE Threads::Threads)
    add_test(NAME cpu_continuation_regression COMMAND cpu_continuation_regression)
    add_executable(cpu_continuation_thread_regression tests/cpu_continuation_regression.cpp)
    target_include_directories(cpu_continuation_thread_regression PRIVATE src/main)
    target_compile_definitions(cpu_continuation_thread_regression PRIVATE AB2_FORCE_THREAD_CONTINUATION)
    target_link_libraries(cpu_continuation_thread_regression PRIVATE Threads::Threads)
    add_test(NAME cpu_continuation_thread_regression COMMAND cpu_continuation_thread_regression)
    add_executable(audio_deadline_regression tests/audio_deadline_regression.cpp src/third_party/ymfm/ymfm_opm.cpp)
    target_include_directories(audio_deadline_regression PRIVATE src/main)
    add_test(NAME audio_deadline_regression COMMAND audio_deadline_regression)
    add_executable(sound_cpu_regression tests/sound_cpu_regression.cpp src/main/hwaudio/xboard_sound_cpu.cpp)
    target_include_directories(sound_cpu_regression PRIVATE src/main)
    add_test(NAME sound_cpu_regression COMMAND sound_cpu_regression)
    add_executable(oracle_sound_boot tests/oracle_sound_boot.cpp src/main/hwaudio/xboard_sound_cpu.cpp)
    target_include_directories(oracle_sound_boot PRIVATE src/main)
    add_test(NAME oracle_sound_boot COMMAND oracle_sound_boot build/oracle/sound_boot.bin)
    set_tests_properties(oracle_sound_boot PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    add_executable(oracle_sound_execution tests/oracle_sound_execution.cpp src/main/hwaudio/xboard_sound_cpu.cpp src/third_party/ymfm/ymfm_opm.cpp)
    target_include_directories(oracle_sound_execution PRIVATE src/main)
    add_test(NAME oracle_sound_execution COMMAND oracle_sound_execution build/oracle/sound_execution.bin build/oracle/sound_execution.txt)
    add_test(NAME oracle_sound_irq COMMAND oracle_sound_execution build/oracle/sound_irq.bin build/oracle/sound_irq.txt)
    set_tests_properties(oracle_sound_execution oracle_sound_irq PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    add_test(NAME oracle_sound_audio_irq COMMAND oracle_sound_execution build/oracle/sound_audio_irq.bin build/oracle/sound_audio_irq.txt --native-audio)
    add_test(NAME oracle_sound_audio_timer_b COMMAND oracle_sound_execution build/oracle/sound_audio_timer_b.bin build/oracle/sound_audio_timer_b.txt --native-audio)
    set_tests_properties(oracle_sound_audio_irq oracle_sound_audio_timer_b PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    add_executable(oracle_pcm tests/oracle_pcm.cpp src/main/romloader.cpp)
    target_include_directories(oracle_pcm PRIVATE src/main)
    add_test(NAME oracle_pcm COMMAND oracle_pcm build/oracle/pcm.bin)
    add_test(NAME oracle_pcm_directed COMMAND oracle_pcm build/oracle/pcm_directed.bin --require-directed)
    add_test(NAME oracle_pcm_synthetic COMMAND oracle_pcm build/oracle/pcm_synthetic.bin --require-directed)
    set_tests_properties(oracle_pcm oracle_pcm_directed oracle_pcm_synthetic PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    add_executable(xboard_timeline_regression tests/xboard_timeline_regression.cpp)
    target_include_directories(xboard_timeline_regression PRIVATE src/main)
    add_test(NAME xboard_timeline_regression COMMAND xboard_timeline_regression)
    add_executable(xboard_irq_lines_regression tests/xboard_irq_lines_regression.cpp)
    target_include_directories(xboard_irq_lines_regression PRIVATE src/main)
    add_test(NAME xboard_irq_lines_regression COMMAND xboard_irq_lines_regression)
    add_executable(mainbus_regression tests/mainbus_regression.cpp
        src/main/engine/mainbus.cpp src/main/engine/subbus.cpp)
    target_include_directories(mainbus_regression PRIVATE src/main)
    add_test(NAME mainbus_regression COMMAND mainbus_regression)
    add_executable(engine_regression
        tests/engine_regression.cpp
        src/main/engine/ohud.cpp
        src/main/engine/osprites.cpp
        src/main/engine/omaininput.cpp
    src/main/engine/omainplayer.cpp
    src/main/engine/omaindamage.cpp src/main/engine/omainsequence.cpp
    src/main/engine/omainworld.cpp src/main/engine/omainweapons.cpp
    src/main/engine/omainobjects.cpp
        src/main/engine/omainhost.cpp
        src/main/engine/omainsound.cpp
        src/main/engine/omainpalette.cpp
    src/main/engine/omainframe.cpp
    src/main/engine/omainsprites.cpp
    src/main/engine/osubsprites.cpp
        src/main/engine/olevelobjs.cpp
        src/main/engine/oengine.cpp
        src/main/engine/subbus.cpp
        src/main/engine/mainbus.cpp
        src/main/engine/oscenery.cpp
        src/main/engine/ostagesetup.cpp
        src/main/engine/osubroad.cpp
        src/main/hwvideo/hwroad.cpp
        src/main/hwvideo/hwsprites.cpp
        src/main/hwvideo/hwtiles.cpp
        src/main/hwvideo/hwpalette.cpp
        src/main/hwvideo/screen.cpp
    )
    target_include_directories(engine_regression PRIVATE src/main)
    add_test(NAME engine_regression COMMAND engine_regression)
endif()

option(ORACLE_TESTS "Enable tests against MAME captures in build/oracle (see make oracle-capture)" OFF)
if(BUILD_TESTING AND ORACLE_TESTS)
    add_executable(oracle_handlers tests/oracle_handlers.cpp src/main/engine/omainsprites.cpp
        src/main/engine/omaininput.cpp src/main/engine/omainplayer.cpp
        src/main/engine/omaindamage.cpp src/main/engine/omainsequence.cpp src/main/engine/omainworld.cpp src/main/engine/omainweapons.cpp
        src/main/engine/omainobjects.cpp src/main/engine/omainhost.cpp
        src/main/engine/omainsound.cpp src/main/engine/mainbus.cpp src/main/engine/subbus.cpp src/main/roms.cpp src/main/romloader.cpp)
    target_include_directories(oracle_handlers PRIVATE src/main)
    foreach(CAPTURE host_entries_directed input input_gameplay input_directed input_endpoints buttons_endpoints player_init player player_directed player_parts player_parts_gameplay player_parts_directed reload reload_directed weapon_control weapon_control_directed world world_directed buttons buttons_directed projectiles projectiles_directed missiles missiles_directed effects_directed collision_directed enemy_weapons_directed enemy_parts_directed enemy_effects enemy_effects_gameplay damage_directed damage_gameplay companion_directed companion_gameplay sequence_directed sequence_gameplay enemy_tracking_directed enemy_tracking_gameplay world_graph_directed refuel_parts_directed)
        add_test(NAME oracle_${CAPTURE} COMMAND oracle_handlers build/oracle/${CAPTURE}.bin)
        set_tests_properties(oracle_${CAPTURE} PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    endforeach()
    add_executable(oracle_host_objects tests/oracle_host_objects.cpp src/main/engine/omainsprites.cpp
        src/main/engine/omainhost.cpp src/main/engine/omaininput.cpp
        src/main/engine/omainplayer.cpp src/main/engine/omaindamage.cpp
        src/main/engine/omainsequence.cpp src/main/engine/omainworld.cpp
        src/main/engine/omainweapons.cpp src/main/engine/omainobjects.cpp
        src/main/engine/omainsound.cpp src/main/engine/mainbus.cpp
        src/main/engine/subbus.cpp src/main/roms.cpp src/main/romloader.cpp)
    target_include_directories(oracle_host_objects PRIVATE src/main)
    foreach(CAPTURE host_objects host_objects_endpoints)
        add_test(NAME oracle_${CAPTURE} COMMAND oracle_host_objects build/oracle/${CAPTURE}.bin)
        set_tests_properties(oracle_${CAPTURE} PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    endforeach()
    set(HOST_ACCEPTANCE_SOURCES
        src/main/engine/omainsprites.cpp src/main/engine/omainhost.cpp
        src/main/engine/omainobjects.cpp src/main/engine/omaininput.cpp
        src/main/engine/omainplayer.cpp src/main/engine/omaindamage.cpp
        src/main/engine/omainsequence.cpp src/main/engine/omainworld.cpp
        src/main/engine/omainweapons.cpp src/main/engine/omainsound.cpp
        src/main/engine/mainbus.cpp src/main/engine/subbus.cpp
        src/main/engine/omainadc.cpp src/main/engine/omainframe.cpp
        src/main/engine/omainpalette.cpp src/main/engine/oscenery.cpp
        src/main/roms.cpp src/main/romloader.cpp)
    add_executable(oracle_host_interleave tests/oracle_host_interleave.cpp ${HOST_ACCEPTANCE_SOURCES})
    add_executable(oracle_frame_host tests/oracle_frame_host.cpp ${HOST_ACCEPTANCE_SOURCES})
    add_executable(oracle_sound_stream tests/oracle_sound_stream.cpp
        src/main/engine/omainsound.cpp src/main/engine/mainbus.cpp src/main/engine/subbus.cpp)
    foreach(TEST oracle_host_interleave oracle_frame_host oracle_sound_stream)
        target_include_directories(${TEST} PRIVATE src/main)
    endforeach()
    add_test(NAME oracle_host_prefix COMMAND oracle_host_interleave build/oracle/host_prefix.bin build/oracle/host_requests.bin)
    add_test(NAME oracle_host_publication COMMAND oracle_host_interleave build/oracle/host_publication.bin)
    add_test(NAME oracle_paired_objects COMMAND oracle_host_interleave build/oracle/paired_objects.bin build/oracle/paired_requests.bin)
    add_test(NAME oracle_paired_frame COMMAND oracle_frame_host build/oracle/paired_frame.bin)
    add_test(NAME oracle_paired_sound COMMAND oracle_sound_stream build/oracle/paired_sound.bin)
    set_tests_properties(oracle_host_prefix oracle_host_publication oracle_paired_objects oracle_paired_frame oracle_paired_sound PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    add_executable(oracle_board tests/oracle_board.cpp)
    target_include_directories(oracle_board PRIVATE src/main)
    foreach(CAPTURE board board_endpoints board_directed)
        add_test(NAME oracle_${CAPTURE} COMMAND oracle_board build/oracle/${CAPTURE}.bin)
        set_tests_properties(oracle_${CAPTURE} PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    endforeach()
    add_executable(oracle_adc_timing tests/oracle_adc_timing.cpp)
    target_include_directories(oracle_adc_timing PRIVATE src/main)
    foreach(CAPTURE adc_timing adc_timing_directed)
        add_test(NAME oracle_${CAPTURE} COMMAND oracle_adc_timing build/oracle/${CAPTURE}.bin)
        set_tests_properties(oracle_${CAPTURE} PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    endforeach()
    add_executable(oracle_irq_service tests/oracle_irq_service.cpp
        src/main/engine/omainirqservice.cpp src/main/engine/omainirqcallee.cpp src/main/engine/mainbus.cpp
        src/main/engine/subbus.cpp src/main/roms.cpp src/main/romloader.cpp)
    target_include_directories(oracle_irq_service PRIVATE src/main)
    foreach(CAPTURE service service_endpoints service_directed)
        add_test(NAME oracle_${CAPTURE} COMMAND oracle_irq_service build/oracle/${CAPTURE}.bin)
        set_tests_properties(oracle_${CAPTURE} PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    endforeach()
    add_executable(oracle_mainirq tests/oracle_mainirq.cpp
        src/main/engine/omainadc.cpp src/main/engine/omainsound.cpp
        src/main/engine/mainbus.cpp src/main/engine/subbus.cpp)
    target_include_directories(oracle_mainirq PRIVATE src/main)
    foreach(CAPTURE mainirq mainirq_endpoints)
        add_test(NAME oracle_${CAPTURE} COMMAND oracle_mainirq build/oracle/${CAPTURE}.bin)
        set_tests_properties(oracle_${CAPTURE} PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    endforeach()
    add_executable(oracle_objects tests/oracle_objects.cpp ${HOST_ACCEPTANCE_SOURCES})
    target_include_directories(oracle_objects PRIVATE src/main)
    foreach(CAPTURE objects objects_gameplay objects_directed object_leaves object_leaves_directed object_calls_directed)
        add_test(NAME oracle_${CAPTURE} COMMAND oracle_objects build/oracle/${CAPTURE}.bin)
        set_tests_properties(oracle_${CAPTURE} PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    endforeach()
    foreach(PORT palette mainsprites mainframe)
        set(PORT_SOURCE src/main/engine/omain${PORT}.cpp)
        if(PORT STREQUAL "mainframe")
            set(PORT_SOURCE src/main/engine/omainframe.cpp)
        elseif(PORT STREQUAL "mainsprites")
            set(PORT_SOURCE src/main/engine/omainsprites.cpp)
        endif()
        add_executable(oracle_${PORT} tests/oracle_${PORT}.cpp ${PORT_SOURCE}
            src/main/romloader.cpp src/main/roms.cpp src/main/engine/mainbus.cpp src/main/engine/subbus.cpp)
        if(PORT STREQUAL "palette")
            target_sources(oracle_${PORT} PRIVATE src/main/engine/omainframe.cpp)
        endif()
        target_include_directories(oracle_${PORT} PRIVATE src/main)
        add_test(NAME oracle_${PORT} COMMAND oracle_${PORT} build/oracle/${PORT}.bin)
        add_test(NAME oracle_${PORT}_directed COMMAND oracle_${PORT} build/oracle/${PORT}_directed.bin)
        set_tests_properties(oracle_${PORT} oracle_${PORT}_directed PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    endforeach()
    add_test(NAME oracle_spriteinit COMMAND oracle_mainsprites build/oracle/spriteinit.bin)
    set_tests_properties(oracle_spriteinit PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    add_executable(oracle_stagesetup tests/oracle_stagesetup.cpp src/main/romloader.cpp src/main/roms.cpp
        src/main/engine/mainbus.cpp src/main/engine/subbus.cpp src/main/engine/ostagesetup.cpp)
    target_include_directories(oracle_stagesetup PRIVATE src/main)
    add_test(NAME oracle_stagesetup COMMAND oracle_stagesetup build/oracle/stagesetup.bin)
    add_test(NAME oracle_stagesetup_control COMMAND oracle_stagesetup build/oracle/stagesetup_control.bin)
    add_test(NAME oracle_stagesetup_alternate COMMAND oracle_stagesetup build/oracle/stagesetup_alternate.bin)
    set_tests_properties(oracle_stagesetup oracle_stagesetup_alternate oracle_stagesetup_control PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    add_executable(oracle_scenery tests/oracle_scenery.cpp src/main/romloader.cpp src/main/roms.cpp
        src/main/engine/mainbus.cpp src/main/engine/subbus.cpp src/main/engine/oscenery.cpp)
    target_include_directories(oracle_scenery PRIVATE src/main)
    add_test(NAME oracle_scenery COMMAND oracle_scenery build/oracle/scenery.bin)
    add_test(NAME oracle_scenery_extended COMMAND oracle_scenery build/oracle/scenery_extended.bin)
    add_test(NAME oracle_scenery_gameplay COMMAND oracle_scenery build/oracle/scenery_gameplay.bin)
    set_tests_properties(oracle_scenery oracle_scenery_extended oracle_scenery_gameplay
        PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    add_executable(oracle_main tests/oracle_main.cpp src/main/romloader.cpp src/main/roms.cpp
        src/main/engine/mainbus.cpp src/main/engine/subbus.cpp src/main/engine/omainadc.cpp src/main/engine/omainsound.cpp)
    target_include_directories(oracle_main PRIVATE src/main)
    add_test(NAME oracle_main COMMAND oracle_main build/oracle/main.bin)
    add_test(NAME oracle_main_gameplay COMMAND oracle_main build/oracle/main_gameplay.bin)
    set_tests_properties(oracle_main oracle_main_gameplay PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    add_executable(oracle_compare tests/oracle_compare.cpp)
    target_include_directories(oracle_compare PRIVATE src/main)
    add_test(NAME oracle_compare COMMAND oracle_compare)
    set_tests_properties(oracle_compare PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    add_executable(oracle_subsprites tests/oracle_subsprites.cpp
        src/main/romloader.cpp src/main/roms.cpp
        src/main/engine/subbus.cpp src/main/engine/osubsprites.cpp)
    target_include_directories(oracle_subsprites PRIVATE src/main)
    add_test(NAME oracle_subsprites COMMAND oracle_subsprites)
    add_test(NAME oracle_subsprites_directed COMMAND oracle_subsprites build/oracle/subsprites_directed.bin)
    add_test(NAME oracle_subsprites_gameplay COMMAND oracle_subsprites build/oracle/subsprites_gameplay.bin)
    set_tests_properties(oracle_subsprites oracle_subsprites_directed oracle_subsprites_gameplay PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    add_executable(oracle_subpublication tests/oracle_subpublication.cpp
        src/main/romloader.cpp src/main/roms.cpp
        src/main/engine/subbus.cpp src/main/engine/osubpublication.cpp)
    target_include_directories(oracle_subpublication PRIVATE src/main)
    add_test(NAME oracle_subpublication_short COMMAND oracle_subpublication build/oracle/subpublication_short.bin)
    add_test(NAME oracle_subpublication_directed COMMAND oracle_subpublication build/oracle/subpublication_directed.bin --require-endpoints)
    add_test(NAME oracle_subpublication COMMAND oracle_subpublication build/oracle/subpublication.bin)
    set_tests_properties(oracle_subpublication_short oracle_subpublication_directed oracle_subpublication PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    add_executable(oracle_subprogress tests/oracle_subprogress.cpp
        src/main/romloader.cpp src/main/roms.cpp src/main/engine/subbus.cpp
        src/main/engine/osubpublication.cpp src/main/engine/osubprogress.cpp)
    target_include_directories(oracle_subprogress PRIVATE src/main)
    add_test(NAME oracle_subprogress_short COMMAND oracle_subprogress build/oracle/subprogress_short.bin)
    add_test(NAME oracle_subprogress_directed COMMAND oracle_subprogress build/oracle/subprogress_directed.bin --require-endpoints)
    add_test(NAME oracle_subprogress_full COMMAND oracle_subprogress build/oracle/subprogress.bin)
    set_tests_properties(oracle_subprogress_short oracle_subprogress_directed oracle_subprogress_full PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    add_executable(oracle_subirq tests/oracle_subirq.cpp
        src/main/romloader.cpp src/main/roms.cpp src/main/engine/subbus.cpp
        src/main/engine/osubpublication.cpp src/main/engine/osubprogress.cpp src/main/engine/osubirq.cpp)
    target_include_directories(oracle_subirq PRIVATE src/main)
    add_test(NAME oracle_subirq_short COMMAND oracle_subirq build/oracle/subirq_short.bin)
    add_test(NAME oracle_subirq_directed COMMAND oracle_subirq build/oracle/subirq_directed.bin --require-endpoints)
    add_test(NAME oracle_subirq_full COMMAND oracle_subirq build/oracle/subirq.bin)
    set_tests_properties(oracle_subirq_short oracle_subirq_directed oracle_subirq_full PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    add_executable(oracle_subreset tests/oracle_subreset.cpp src/main/engine/osubirq.cpp
        src/main/engine/osubprogress.cpp src/main/engine/osubpublication.cpp
        src/main/engine/subbus.cpp src/main/roms.cpp src/main/romloader.cpp)
    target_include_directories(oracle_subreset PRIVATE src/main)
    add_test(NAME oracle_subreset COMMAND oracle_subreset build/oracle/subreset.bin)
    set_tests_properties(oracle_subreset PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    add_executable(oracle_bus_arbiter tests/oracle_bus_arbiter.cpp src/main/engine/mainbus.cpp src/main/engine/subbus.cpp)
    target_include_directories(oracle_bus_arbiter PRIVATE src/main)
    add_executable(oracle_publication_schedule tests/oracle_publication_schedule.cpp src/main/hwaudio/xboard_sound_cpu.cpp src/third_party/ymfm/ymfm_opm.cpp
        src/main/hwvideo/hwsprites.cpp src/main/hwvideo/hwroad.cpp src/main/hwvideo/hwtiles.cpp src/main/hwvideo/hwpalette.cpp src/main/hwvideo/screen.cpp
        src/main/engine/omainirqservice.cpp src/main/engine/omainirqcallee.cpp src/main/engine/mainbus.cpp
        src/main/engine/osubpublication.cpp src/main/engine/osubprogress.cpp src/main/engine/osubirq.cpp src/main/engine/subbus.cpp src/main/roms.cpp src/main/romloader.cpp)
    target_include_directories(oracle_publication_schedule PRIVATE src/main)
    target_link_libraries(oracle_publication_schedule PRIVATE Threads::Threads)
    add_test(NAME oracle_publication_schedule COMMAND oracle_publication_schedule build/oracle/publication_schedule.bin build/oracle/publication_schedule.txt)
    add_test(NAME oracle_joint_main_progress COMMAND oracle_publication_schedule build/oracle/joint_main_progress.bin build/oracle/joint_main_progress.txt --require-main-closed --require-sub-continuity --require-video-frontiers --require-sound-handshake)
    add_test(NAME oracle_joint_reset_window COMMAND oracle_publication_schedule build/oracle/joint_reset_focused.bin build/oracle/joint_reset_focused.txt --reset-window --physical-main --require-sub-continuity --require-video-frontiers --require-sound-handshake)
    add_test(NAME oracle_joint_main_progress_full COMMAND oracle_publication_schedule build/oracle/joint_main_progress_full.bin build/oracle/joint_main_progress_full.txt --require-reset --require-main-closed --require-sub-continuity --require-video-frontiers --require-sound-handshake)
    add_test(NAME oracle_joint_sound_full COMMAND oracle_publication_schedule build/oracle/joint_sound_full.bin build/oracle/joint_sound_full.txt --require-reset --require-main-closed --require-sub-continuity --require-video-rgb --require-native-sound)
    set_tests_properties(oracle_joint_sound_full PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    add_test(NAME oracle_joint_audio_full COMMAND oracle_publication_schedule build/oracle/joint_audio_full.bin build/oracle/joint_audio_full.txt --require-reset --require-main-closed --require-sub-continuity --require-video-rgb --require-native-deadlines)
    set_tests_properties(oracle_joint_audio_full PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    add_test(NAME oracle_joint_video COMMAND oracle_publication_schedule build/oracle/joint_main_progress_full.bin build/oracle/joint_main_progress_full.txt --require-reset --require-main-closed --require-sub-continuity --require-video-rgb --require-sound-handshake)
    add_test(NAME oracle_joint_video_directed COMMAND oracle_publication_schedule build/oracle/joint_video_directed.bin build/oracle/joint_video_directed.txt --require-video-fixture --require-main-closed --require-sub-continuity --require-video-rgb --require-sound-handshake)
    set_tests_properties(oracle_joint_video oracle_joint_video_directed PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    set_tests_properties(oracle_publication_schedule oracle_joint_main_progress oracle_joint_reset_window oracle_joint_main_progress_full PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    add_test(NAME oracle_bus_arbiter COMMAND oracle_bus_arbiter build/oracle/bus_arbiter_short.bin build/oracle/arbiter_schedule.txt)
    set_tests_properties(oracle_bus_arbiter PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    add_test(NAME oracle_bus_boot COMMAND oracle_bus_arbiter build/oracle/bus_arbiter_boot.bin build/oracle/arbiter_boot.txt --require-boot)
    set_tests_properties(oracle_bus_boot PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    add_executable(oracle_subroad tests/oracle_subroad.cpp
        src/main/romloader.cpp src/main/roms.cpp
        src/main/engine/subbus.cpp src/main/engine/osubroad.cpp)
    target_include_directories(oracle_subroad PRIVATE src/main)
    add_test(NAME oracle_subroad COMMAND oracle_subroad)
    add_test(NAME oracle_roadrequests COMMAND oracle_subroad build/oracle/roadrequests.bin)
    add_test(NAME oracle_roadinit COMMAND oracle_subroad build/oracle/roadinit.bin)
    add_test(NAME oracle_subroad_special COMMAND oracle_subroad build/oracle/subroad_special.bin)
    add_test(NAME oracle_subroad_gameplay COMMAND oracle_subroad build/oracle/subroad_gameplay.bin)
    set_tests_properties(oracle_subroad oracle_subroad_gameplay oracle_roadrequests oracle_roadinit oracle_subroad_special PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    add_executable(oracle_video tests/oracle_video.cpp
        src/main/romloader.cpp src/main/roms.cpp
        src/main/hwvideo/hwroad.cpp src/main/hwvideo/hwsprites.cpp src/main/hwvideo/hwtiles.cpp
        src/main/hwvideo/hwpalette.cpp src/main/hwvideo/screen.cpp src/main/engine/mainbus.cpp src/main/engine/subbus.cpp src/main/engine/omainsprites.cpp src/main/engine/osubsprites.cpp src/main/engine/osubroad.cpp)
    target_include_directories(oracle_video PRIVATE src/main)
    add_test(NAME oracle_video COMMAND oracle_video build/oracle/frames.bin)
    add_test(NAME oracle_video_nosprites COMMAND oracle_video build/oracle/frames_nosprites.bin)
    add_test(NAME oracle_paired COMMAND oracle_video build/oracle/paired.bin 0 build/oracle/paired_requests.bin)
    set_tests_properties(oracle_paired PROPERTIES DEPENDS oracle_paired_objects WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    add_test(NAME oracle_paired_road_objects COMMAND oracle_host_interleave build/oracle/paired_road_objects.bin build/oracle/paired_road_requests.bin)
    add_test(NAME oracle_paired_road_frame COMMAND oracle_frame_host build/oracle/paired_road_frame.bin)
    add_test(NAME oracle_paired_road_sound COMMAND oracle_sound_stream build/oracle/paired_road_sound.bin)
    add_test(NAME oracle_paired_road COMMAND oracle_video build/oracle/paired_road.bin 0 build/oracle/paired_road_requests.bin)
    set_tests_properties(oracle_paired_road_objects oracle_paired_road_frame oracle_paired_road_sound oracle_paired_road PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    set_tests_properties(oracle_paired_road PROPERTIES DEPENDS oracle_paired_road_objects)
    add_test(NAME oracle_pipeline COMMAND oracle_video build/oracle/pipeline.bin)
    add_test(NAME oracle_video_gameplay COMMAND oracle_video build/oracle/frames_gameplay.bin)
    set_tests_properties(oracle_video oracle_video_nosprites oracle_video_gameplay oracle_pipeline PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
endif()

option(STAGE1_ROM_TESTS "Enable tests requiring the user's Stage 1 ROM set" OFF)
if(BUILD_TESTING AND STAGE1_ROM_TESTS)
    get_target_property(STAGE1_SOURCES afterburner2 SOURCES)
    list(REMOVE_ITEM STAGE1_SOURCES src/main/main.cpp)
    add_executable(stage1_integration tests/stage1_integration.cpp ${STAGE1_SOURCES})
    target_include_directories(stage1_integration PRIVATE src/main)
    add_test(NAME stage1_integration COMMAND stage1_integration)
    set_tests_properties(stage1_integration PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
endif()

if(BUILD_TESTING AND ORACLE_TESTS)
    foreach(PORT irq4_service logic_wait caller)
        add_executable(oracle_${PORT} tests/oracle_${PORT}.cpp
            src/main/engine/omainirqservice.cpp src/main/engine/omainirqcallee.cpp src/main/engine/mainbus.cpp
            src/main/engine/subbus.cpp src/main/roms.cpp src/main/romloader.cpp)
        target_include_directories(oracle_${PORT} PRIVATE src/main)
    endforeach()
    add_test(NAME oracle_irq4_service_gameflow_actions COMMAND oracle_irq4_service build/oracle/irq4_service_gameflow_actions.bin --require-actions)
    set_tests_properties(oracle_irq4_service_gameflow_actions PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    add_test(NAME oracle_irq4_service_gameflow_endpoints COMMAND oracle_irq4_service build/oracle/irq4_service_gameflow_endpoints.bin --require-gameflow)
    set_tests_properties(oracle_irq4_service_gameflow_endpoints PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    add_test(NAME oracle_irq4_service_rts_callees COMMAND oracle_irq4_service build/oracle/irq4_service_rts_callees.bin --require-rts)
    set_tests_properties(oracle_irq4_service_rts_callees PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    foreach(CAPTURE irq4_service irq4_service_directed irq4_service_edges irq4_service_road_endpoints irq4_service_cycle_endpoints irq4_service_callee_endpoints irq4_service_scenery_endpoints)
        add_test(NAME oracle_${CAPTURE} COMMAND oracle_irq4_service build/oracle/${CAPTURE}.bin)
        set_tests_properties(oracle_${CAPTURE} PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    endforeach()
    foreach(CAPTURE irq4_return irq4_return_edges)
        add_test(NAME oracle_${CAPTURE} COMMAND oracle_irq4_service build/oracle/${CAPTURE}.bin build/oracle/${CAPTURE}.bin.returns)
        set_tests_properties(oracle_${CAPTURE} PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    endforeach()
    foreach(CAPTURE logic_wait logic_wait_directed)
        add_test(NAME oracle_${CAPTURE} COMMAND oracle_logic_wait build/oracle/${CAPTURE}.bin)
        set_tests_properties(oracle_${CAPTURE} PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    endforeach()
    foreach(CAPTURE caller caller_directed caller_service caller_service_edges caller_return caller_return_edges caller_steps)
        add_test(NAME oracle_${CAPTURE} COMMAND oracle_caller build/oracle/${CAPTURE}.bin)
        set_tests_properties(oracle_${CAPTURE} PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    endforeach()

    add_test(NAME oracle_caller_words COMMAND oracle_caller build/oracle/caller_words.bin build/oracle/caller_steps.bin)
    set_tests_properties(oracle_caller_words PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    add_test(NAME oracle_caller_successors COMMAND oracle_caller build/oracle/caller_successors.bin build/oracle/caller_words.bin)
    add_test(NAME oracle_caller_successors_directed COMMAND oracle_caller build/oracle/caller_successors_directed.bin --require-taken)
    set_tests_properties(oracle_caller_successors oracle_caller_successors_directed PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    add_executable(oracle_cpu_visibility tests/oracle_cpu_visibility.cpp src/main/engine/mainbus.cpp src/main/engine/subbus.cpp)
    target_include_directories(oracle_cpu_visibility PRIVATE src/main)
    add_test(NAME oracle_caller_closure COMMAND oracle_caller build/oracle/caller_closure.bin build/oracle/caller_successors.bin --require-closed)
    add_test(NAME oracle_caller_closure_directed COMMAND oracle_caller build/oracle/caller_closure_directed.bin --require-taken --require-closed)
    set_tests_properties(oracle_caller_closure oracle_caller_closure_directed PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

    add_test(NAME oracle_caller_follower COMMAND oracle_caller build/oracle/caller_follower.bin --require-closed)
    add_test(NAME oracle_caller_follower_directed COMMAND oracle_caller build/oracle/caller_follower_directed.bin --require-follower --require-closed)
    set_tests_properties(oracle_caller_follower oracle_caller_follower_directed PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

    add_test(NAME oracle_caller_ready COMMAND oracle_caller build/oracle/caller_ready.bin --require-ready-path --require-worldinit --require-publication-init --require-closed)
    add_test(NAME oracle_caller_ready_directed COMMAND oracle_caller build/oracle/caller_ready_directed.bin --require-ready --require-closed)
    add_test(NAME oracle_caller_worldinit_directed COMMAND oracle_caller build/oracle/caller_worldinit_directed.bin --require-worldinit --require-closed)
    set_tests_properties(oracle_caller_ready oracle_caller_ready_directed oracle_caller_worldinit_directed PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

    add_test(NAME oracle_caller_allirq COMMAND oracle_caller build/oracle/caller_allirq_trail.bin --require-ready-path --require-worldinit --require-publication-init --require-clear --require-trail-path --require-allirq --require-closed)
    add_test(NAME oracle_caller_trail_directed COMMAND oracle_caller build/oracle/caller_trail_directed.bin --require-trail --require-closed)
    set_tests_properties(oracle_caller_allirq oracle_caller_trail_directed PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

    add_test(NAME oracle_caller_soundproducer COMMAND oracle_caller build/oracle/caller_soundproducer.bin --require-soundproducer-path --require-allirq --require-closed)
    add_test(NAME oracle_caller_soundproducer_directed COMMAND oracle_caller build/oracle/caller_soundproducer_directed.bin --require-soundproducer --require-closed)
    set_tests_properties(oracle_caller_soundproducer oracle_caller_soundproducer_directed PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    add_test(NAME oracle_caller_input_short COMMAND oracle_caller build/oracle/caller_input_short.bin --require-input-path --require-closed)
    add_test(NAME oracle_caller_input_directed COMMAND oracle_caller build/oracle/caller_input_directed.bin --require-input --require-closed)
    add_test(NAME oracle_caller_input COMMAND oracle_caller build/oracle/caller_input.bin --require-input-path --require-allirq --require-closed)
    set_tests_properties(oracle_caller_input_short oracle_caller_input_directed oracle_caller_input PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    add_test(NAME oracle_caller_weapon_full COMMAND oracle_caller build/oracle/caller_weapon.bin --require-weapon-path --require-flight-path --require-input-path --require-allirq --require-closed)
    add_test(NAME oracle_caller_weapon_short COMMAND oracle_caller build/oracle/caller_weapon_short.bin --require-weapon-path --require-closed)
    add_test(NAME oracle_caller_weapon_directed COMMAND oracle_caller build/oracle/caller_weapon_directed.bin --require-weapon --require-closed)
    set_tests_properties(oracle_caller_weapon_full oracle_caller_weapon_short oracle_caller_weapon_directed PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    add_test(NAME oracle_caller_host_short COMMAND oracle_caller build/oracle/caller_host_short.bin --require-closed)
    add_test(NAME oracle_caller_host_full COMMAND oracle_caller build/oracle/caller_host.bin --require-host-path --require-weapon-path --require-flight-path --require-input-path --require-allirq --require-closed)
    add_test(NAME oracle_caller_host_directed COMMAND oracle_caller build/oracle/caller_host_directed.bin --require-host --require-closed)
    set_tests_properties(oracle_caller_host_short oracle_caller_host_full oracle_caller_host_directed PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    add_test(NAME oracle_caller_companion_short COMMAND oracle_caller build/oracle/caller_companion_short.bin --require-companion-path --require-closed)
    add_test(NAME oracle_caller_companion_directed COMMAND oracle_caller build/oracle/caller_companion_directed.bin --require-companion --require-closed)
    add_test(NAME oracle_caller_companion COMMAND oracle_caller build/oracle/caller_companion.bin --require-companion-path --require-flight-path --require-input-path --require-allirq --require-closed)
    set_tests_properties(oracle_caller_companion_short oracle_caller_companion_directed oracle_caller_companion PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    add_test(NAME oracle_caller_flight_short COMMAND oracle_caller build/oracle/caller_flight_short.bin --require-flight-path --require-closed)
    add_test(NAME oracle_caller_flight_directed COMMAND oracle_caller build/oracle/caller_flight_directed.bin --require-flight --require-closed)
    add_test(NAME oracle_caller_flight COMMAND oracle_caller build/oracle/caller_flight.bin --require-flight-path --require-input-path --require-allirq --require-closed)
    set_tests_properties(oracle_caller_flight_short oracle_caller_flight_directed oracle_caller_flight PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    foreach(CAPTURE caller_host_short caller_host caller_host_directed caller_companion_short caller_companion_directed caller_companion caller_flight_short caller_flight_directed caller_flight caller_input_short caller_input_directed caller_input caller_soundproducer caller_soundproducer_directed caller_allirq_trail caller_trail_directed caller_ready caller_ready_directed caller_worldinit_directed caller_follower caller_follower_directed caller caller_directed caller_service caller_service_edges caller_return caller_return_edges caller_steps caller_words caller_successors caller_successors_directed caller_closure caller_closure_directed)
        add_test(NAME oracle_visibility_${CAPTURE} COMMAND oracle_cpu_visibility build/oracle/${CAPTURE}.bin.visibility)
        set_tests_properties(oracle_visibility_${CAPTURE} PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    endforeach()

endif()

if(BUILD_TESTING AND ORACLE_TESTS)
add_executable(oracle_speaker tests/oracle_speaker.cpp)
target_include_directories(oracle_speaker PRIVATE src/main)
add_custom_target(oracle-speaker
    COMMAND $<TARGET_FILE:oracle_speaker> build/oracle/joint_speaker_full.bin.audio
    COMMAND $<TARGET_FILE:oracle_speaker> build/oracle/speaker_edges
    DEPENDS oracle_speaker WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
add_custom_target(oracle-joint-speaker-full
    COMMAND $<TARGET_FILE:oracle_publication_schedule> build/oracle/joint_speaker_full.bin build/oracle/joint_speaker_full.txt --require-reset --require-main-closed --require-sub-continuity --require-video-rgb --require-native-deadlines --require-speaker
    DEPENDS oracle_publication_schedule WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
add_custom_target(oracle-speaker-mutations
    COMMAND python3 tools/oracle/check_speaker_mutations.py
    DEPENDS oracle-speaker oracle-joint-speaker-full WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
add_test(NAME oracle_speaker COMMAND oracle_speaker build/oracle/joint_speaker_full.bin.audio)
add_test(NAME oracle_speaker_edges COMMAND oracle_speaker build/oracle/speaker_edges)
add_test(NAME oracle_joint_speaker_full COMMAND oracle_publication_schedule build/oracle/joint_speaker_full.bin build/oracle/joint_speaker_full.txt --require-reset --require-main-closed --require-sub-continuity --require-video-rgb --require-native-deadlines --require-speaker)
add_test(NAME oracle_speaker_mutations COMMAND python3 tools/oracle/check_speaker_mutations.py)
set_tests_properties(oracle_speaker oracle_speaker_edges oracle_joint_speaker_full oracle_speaker_mutations PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
add_test(NAME oracle_joint_video_owned_directed COMMAND oracle_publication_schedule build/oracle/joint_video_owned_directed.bin build/oracle/joint_video_owned_directed.txt --require-video-fixture --require-main-closed --require-sub-continuity --require-video-rgb --require-native-deadlines --require-speaker)
set_tests_properties(oracle_joint_video_owned_directed PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
endif()

if(BUILD_TESTING AND ORACLE_TESTS)
    foreach(PORT cpu_coverage cpu_nested_irq)
        add_executable(oracle_${PORT} tests/oracle_${PORT}.cpp
            src/main/engine/omainirqservice.cpp src/main/engine/omainirqcallee.cpp src/main/engine/mainbus.cpp
            src/main/engine/osubirq.cpp src/main/engine/osubprogress.cpp src/main/engine/osubpublication.cpp
            src/main/engine/subbus.cpp src/main/roms.cpp src/main/romloader.cpp)
        target_include_directories(oracle_${PORT} PRIVATE src/main)
        target_link_libraries(oracle_${PORT} PRIVATE Threads::Threads)
    endforeach()
    add_test(NAME oracle_cpu_coverage COMMAND oracle_cpu_coverage build/oracle/cpu_coverage.bin)
    add_test(NAME oracle_recovery COMMAND oracle_cpu_coverage build/oracle/cpu_recovery.bin)
    add_test(NAME oracle_recovery_coverage COMMAND python3 tools/oracle/check_recovery_coverage.py)
    add_test(NAME oracle_recovery_mutations COMMAND python3 tools/oracle/check_cpu_coverage_mutations.py --recovery)
    set_tests_properties(oracle_recovery oracle_recovery_coverage oracle_recovery_mutations PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    set_tests_properties(oracle_recovery PROPERTIES FIXTURES_SETUP recovery_census)
    set_tests_properties(oracle_recovery_coverage oracle_recovery_mutations PROPERTIES FIXTURES_REQUIRED recovery_census)
    add_test(NAME oracle_cpu_coverage_legacy COMMAND oracle_cpu_coverage build/oracle/cpu_coverage_expanded.bin)
    add_test(NAME oracle_cpu_coverage_expanded COMMAND oracle_cpu_coverage build/oracle/cpu_coverage_expanded_full.bin)
    add_test(NAME oracle_cpu_nested_irq COMMAND oracle_cpu_nested_irq build/oracle/cpu_nested_irq.bin)
    add_test(NAME oracle_cpu_coverage_check COMMAND python3 tools/oracle/check_cpu_coverage.py build/oracle/cpu_coverage_plan.json build/oracle/cpu_coverage_expanded_full.bin.coverage.json)
    set_tests_properties(oracle_cpu_coverage_expanded PROPERTIES FIXTURES_SETUP cpu_coverage_census)
    set_tests_properties(oracle_cpu_coverage_check PROPERTIES FIXTURES_REQUIRED cpu_coverage_census)
    add_test(NAME oracle_joint_coverage_flight COMMAND oracle_publication_schedule build/oracle/joint_coverage_flight_expanded.bin build/oracle/joint_coverage_flight_expanded.txt --require-reset --require-main-closed --require-sub-continuity --require-video-rgb --require-sound-handshake)
    set_tests_properties(oracle_cpu_coverage oracle_cpu_coverage_legacy oracle_cpu_coverage_expanded oracle_cpu_nested_irq oracle_cpu_coverage_check oracle_joint_coverage_flight PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    add_custom_target(oracle_cpu_coverage_mutations
        COMMAND python3 tools/oracle/check_cpu_coverage_mutations.py
        DEPENDS oracle_cpu_coverage oracle_cpu_nested_irq WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endif()

if(BUILD_TESTING AND ORACLE_TESTS)
add_test(NAME oracle_joint_cold COMMAND oracle_publication_schedule build/oracle/joint_cold.bin build/oracle/joint_cold.txt --require-cold --require-main-closed --require-sub-continuity --require-video-rgb --require-native-deadlines --require-speaker)
add_test(NAME oracle_joint_cold_full COMMAND oracle_publication_schedule build/oracle/joint_cold_full.bin build/oracle/joint_cold_full.txt --require-reset --require-cold --require-main-closed --require-sub-continuity --require-video-rgb --require-native-deadlines --require-speaker)
add_test(NAME oracle_joint_cold_boundaries COMMAND python3 tools/oracle/check_joint_cold_boundaries.py build/oracle/joint_cold_full.bin build/oracle/joint_speaker_full.bin)
add_test(NAME oracle_joint_cold_mutations COMMAND python3 tools/oracle/check_joint_cold_mutations.py)
set_tests_properties(oracle_joint_cold PROPERTIES FIXTURES_SETUP cold_initial)
set_tests_properties(oracle_joint_cold_mutations PROPERTIES FIXTURES_REQUIRED cold_initial)
set_tests_properties(oracle_joint_cold_full PROPERTIES FIXTURES_SETUP cold_census)
set_tests_properties(oracle_joint_cold_boundaries PROPERTIES FIXTURES_REQUIRED cold_census)
set_tests_properties(oracle_joint_cold oracle_joint_cold_full oracle_joint_cold_boundaries oracle_joint_cold_mutations PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
endif()

if(BUILD_TESTING AND ORACLE_TESTS)
    get_target_property(WHOLE_HOST_ORACLE_SOURCES oracle_publication_schedule SOURCES)
    list(REMOVE_ITEM WHOLE_HOST_ORACLE_SOURCES tests/oracle_publication_schedule.cpp)
    add_executable(oracle_whole_host tests/oracle_whole_host.cpp ${WHOLE_HOST_ORACLE_SOURCES})
    target_include_directories(oracle_whole_host PRIVATE src/main)
    target_link_libraries(oracle_whole_host PRIVATE Threads::Threads)
    foreach(corpus IN ITEMS cold cold_full)
        add_test(NAME oracle_whole_host_${corpus} COMMAND oracle_whole_host build/oracle/joint_${corpus}.bin build/oracle/joint_${corpus}.txt)
        set_tests_properties(oracle_whole_host_${corpus} PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    endforeach()
    add_test(NAME oracle_whole_host_expanded COMMAND oracle_whole_host build/oracle/whole_host_expanded.bin build/oracle/whole_host_expanded.txt)
    add_test(NAME oracle_whole_host_mutations COMMAND python3 tools/oracle/check_whole_host_mutations.py)
    set_tests_properties(oracle_whole_host_expanded oracle_whole_host_mutations PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    set_tests_properties(oracle_whole_host_cold PROPERTIES FIXTURES_SETUP whole_host_initial)
    set_tests_properties(oracle_whole_host_mutations PROPERTIES FIXTURES_REQUIRED whole_host_initial)
endif()

if(BUILD_TESTING AND STAGE1_ROM_TESTS)
    get_target_property(WHOLE_HOST_GAME_SOURCES afterburner2 SOURCES)
    list(REMOVE_ITEM WHOLE_HOST_GAME_SOURCES src/main/main.cpp)
    add_executable(whole_host_integration tests/whole_host_integration.cpp ${WHOLE_HOST_GAME_SOURCES})
    target_include_directories(whole_host_integration PRIVATE src/main ${SDL2_INCLUDE_DIRS})
    target_link_libraries(whole_host_integration PRIVATE ${SDL2_LIBRARIES} Threads::Threads)
    add_test(NAME whole_host_integration COMMAND whole_host_integration)
    set_tests_properties(whole_host_integration PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    add_executable(game_session_integration tests/game_session_integration.cpp ${WHOLE_HOST_GAME_SOURCES})
    target_include_directories(game_session_integration PRIVATE src/main ${SDL2_INCLUDE_DIRS})
    target_link_libraries(game_session_integration PRIVATE ${SDL2_LIBRARIES} Threads::Threads)
    add_test(NAME game_session_integration COMMAND game_session_integration)
    set_tests_properties(game_session_integration PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    add_test(NAME whole_host_attract COMMAND afterburner2 --frames 2400)
    set_tests_properties(whole_host_attract PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
endif()
