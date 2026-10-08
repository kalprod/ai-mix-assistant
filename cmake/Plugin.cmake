# JUCE plugin, UI snapshot tool and in-process host checks.

if (AIMIX_JUCE_PATH)
    add_subdirectory (${AIMIX_JUCE_PATH} ${CMAKE_BINARY_DIR}/JUCE EXCLUDE_FROM_ALL)
else()
    include (FetchContent)
    FetchContent_Declare (JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.10
        GIT_SHALLOW    ON)
    FetchContent_MakeAvailable (JUCE)
endif()

set (AIMIX_FORMATS VST3 Standalone)
if (APPLE)
    list (APPEND AIMIX_FORMATS AU)
endif()
if (AIMIX_AAX_SDK_PATH)
    juce_set_aax_sdk_path (${AIMIX_AAX_SDK_PATH})
    list (APPEND AIMIX_FORMATS AAX)
endif()

juce_add_plugin (AIMixAssistant
    COMPANY_NAME                 "KAL"
    BUNDLE_ID                    "com.aimix.mixassistant"
    PLUGIN_MANUFACTURER_CODE     Aimx
    PLUGIN_CODE                  Aima
    PRODUCT_NAME                 "K MASTER"
    FORMATS                      ${AIMIX_FORMATS}
    IS_SYNTH                     FALSE
    NEEDS_MIDI_INPUT             FALSE
    NEEDS_MIDI_OUTPUT            FALSE
    IS_MIDI_EFFECT               FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS  FALSE
    VST3_CATEGORIES              Fx Analyzer
    AU_MAIN_TYPE                 kAudioUnitType_Effect
    AAX_CATEGORY                 AAX_ePlugInCategory_None
    COPY_PLUGIN_AFTER_BUILD      FALSE)

set (AIMIX_UI_SOURCES
    plugin/ui/ChannelRack.cpp
    plugin/ui/DiagnosticCards.cpp
    plugin/ui/MasterView.cpp
    plugin/ui/ListenerView.cpp
    plugin/ui/ChainPanel.cpp
    plugin/ui/StylePicker.cpp)

set (AIMIX_LIBRARY_SOURCES
    plugin/library/PluginScanner.cpp
    plugin/library/PluginLibraryService.cpp)
if (APPLE)
    list (APPEND AIMIX_LIBRARY_SOURCES plugin/library/AuScan.mm)
endif()

target_sources (AIMixAssistant PRIVATE
    plugin/PluginProcessor.cpp
    plugin/PluginEditor.cpp
    ${AIMIX_UI_SOURCES}
    ${AIMIX_LIBRARY_SOURCES})

target_compile_definitions (AIMixAssistant PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries (AIMixAssistant
    PRIVATE
        aimix_core
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if (APPLE)
    target_link_libraries (AIMixAssistant PRIVATE "-framework AudioToolbox")
endif()

# ---------------------------------------------------------------------------
# Host-like checks against the real AudioProcessor + editor, and PNG snapshots
# of the UI driven by the synthetic session. Runs headless (xvfb on Linux).
juce_add_console_app (aimix_plugin_checks PRODUCT_NAME "AIMix Plugin Checks")
target_sources (aimix_plugin_checks PRIVATE tools/PluginChecks.cpp)
target_compile_definitions (aimix_plugin_checks PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries (aimix_plugin_checks PRIVATE
    AIMixAssistant
    aimix_core
    juce::juce_audio_utils
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
