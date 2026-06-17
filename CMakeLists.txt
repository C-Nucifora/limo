cmake_minimum_required(VERSION 3.25)

project(Limo VERSION 1.2.2 LANGUAGES CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(IS_FLATPAK "Whether this is being built for a flatpak." OFF)
option(USE_SYSTEM_LIBUNRAR "Whether to use the system version of libunrar." OFF)
option(LIMO_WITH_LOOT "Build the LOOT/OpenMW deployers (Bethesda/Morrowind support; requires libloot)." OFF)
option(LIMO_WITH_UNRAR "Build RAR-archive extraction support (requires libunrar)." OFF)
option(LIMO_BUILD_TESTS "Build the Catch2 test suite." OFF)

# jsoncpp
find_package(PkgConfig REQUIRED)
pkg_check_modules(JSONCPP jsoncpp)

# libarchive
find_package(LibArchive REQUIRED)

# pugixml
pkg_check_modules(PUGIXML pugixml)
find_package(pugixml REQUIRED)

# cpr
find_package(cpr REQUIRED)

# OpenSSL
find_package(OpenSSL REQUIRED)

# Qt
find_package(QT NAMES Qt6 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Svg Network)

# fork #22: LinguistTools is OPTIONAL. It is only used to compile the .ts
# translation sources into .qm files. The build (LOOT on and LOOT off) must
# succeed even when it is unavailable, so it is requested QUIET and never REQUIRED.
find_package(Qt${QT_VERSION_MAJOR} COMPONENTS LinguistTools QUIET)

# libunrar (only required for RAR-archive extraction support)
if(LIMO_WITH_UNRAR)
if(NOT DEFINED LIBUNRAR_INCLUDE_DIR)
  if(IS_FLATPAK)
    set(LIBUNRAR_INCLUDE_DIR "/app/include/unrar")
  else()
    if(USE_SYSTEM_LIBUNRAR)
      find_file(LIBUNRAR_DLL_PATH "unrar/dll.hpp" REQUIRED)
      cmake_path(GET LIBUNRAR_DLL_PATH PARENT_PATH LIBUNRAR_INCLUDE_DIR)
    else()
      set(LIBUNRAR_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/unrar")
    endif()
  endif()
endif()
if(NOT DEFINED LIBUNRAR_PATH)
  if(IS_FLATPAK)
    set(LIBUNRAR_PATH "/app/lib/libunrar.a")
  else()
    if(USE_SYSTEM_LIBUNRAR)
      find_library(LIBUNRAR_PATH libunrar.a)
      if(${LIBUNRAR_PATH} STREQUAL "LIBUNRAR_PATH-NOTFOUND")
        find_library(LIBUNRAR_PATH libunrar.so REQUIRED)
      endif()
    else()
      set(LIBUNRAR_PATH "${PROJECT_SOURCE_DIR}/unrar/libunrar.a")
    endif()
  endif()
endif()
else()
  set(LIBUNRAR_INCLUDE_DIR "")
  set(LIBUNRAR_PATH "")
endif()

# libloot (only required for the LOOT / OpenMW deployers)
if(LIMO_WITH_LOOT)
    if(NOT DEFINED LIBLOOT_INCLUDE_DIR)
        find_file(LIBLOOT_API_PATH "loot/api.h" REQUIRED)
        cmake_path(GET LIBLOOT_API_PATH PARENT_PATH LIBLOOT_INCLUDE_DIR)
    endif()
    if(NOT DEFINED LIBLOOT_PATH)
        find_library(LIBLOOT_PATH libloot.so REQUIRED)
    endif()
else()
    set(LIBLOOT_INCLUDE_DIR "")
    set(LIBLOOT_PATH "")
endif()

# lz4
pkg_check_modules(LZ4 REQUIRED liblz4)

# zstd
pkg_check_modules(ZSTD REQUIRED libzstd)

# zlib
find_package(ZLIB REQUIRED)

# Separated for tests
set(CORE_SOURCES
        src/core/appinfo.h
        src/core/archivenormalizer.cpp
        src/core/archivenormalizer.h
        src/core/autotag.cpp
        src/core/autotag.h
        src/core/backupmanager.cpp
        src/core/backupmanager.h
        src/core/backuptarget.cpp
        src/core/backuptarget.h
        src/core/bg3deployer.cpp
        src/core/bg3deployer.h
        src/core/bg3pakfile.cpp
        src/core/bg3pakfile.h
        src/core/bg3plugin.cpp
        src/core/bg3plugin.h
        src/core/casematchingdeployer.cpp
        src/core/casematchingdeployer.h
        src/core/cyberpunkdeployer.cpp
        src/core/cyberpunkdeployer.h
        src/core/cyberpunkredmod.cpp
        src/core/cyberpunkredmod.h
        src/core/cyberpunksetup.cpp
        src/core/cyberpunksetup.h
        src/core/tw3deployer.cpp
        src/core/tw3deployer.h
        src/core/tw3mergeutil.cpp
        src/core/tw3mergeutil.h
        src/core/tw3scriptmerge.cpp
        src/core/tw3scriptmerge.h
        src/core/changelogentry.cpp
        src/core/changelogentry.h
        # fork #197: Wabbajack .wabbajack modlist parser
        src/core/wabbajackmodlist.cpp
        src/core/wabbajackmodlist.h
        src/core/compressionerror.h
        src/core/conflictinfo.h
        src/core/consts.h
        src/core/cryptography.cpp
        src/core/remote/remotesource.h
        src/core/remote/remotesourceregistry.h
        src/core/remote/thunderstoreprovider.cpp
        src/core/remote/thunderstoreprovider.h
        src/core/remote/gamebanana_provider.cpp
        src/core/remote/gamebanana_provider.h
        src/core/remote/modio_provider.cpp
        src/core/remote/modio_provider.h
        src/core/cryptography.h
        src/core/importers/mo2importer.cpp
        src/core/importers/mo2importer.h
        src/core/deployer.cpp
        src/core/deployer.h
        src/core/deployerfactory.cpp
        src/core/deployerfactory.h
        src/core/overlaydeployer.cpp
        src/core/overlaydeployer.h
        src/core/deployerinfo.h
        src/core/editapplicationinfo.h
        src/core/editautotagaction.cpp
        src/core/editautotagaction.h
        src/core/editdeployerinfo.h
        src/core/editmanualtagaction.cpp
        src/core/editmanualtagaction.h
        src/core/editprofileinfo.h
        src/core/externalchangesinfo.h
        src/core/filechangechoices.h
        src/core/fomod/dependency.cpp
        src/core/fomod/dependency.h
        src/core/fomod/file.h
        src/core/fomod/fomodinstaller.cpp
        src/core/fomod/fomodinstaller.h
        src/core/fomod/plugin.h
        src/core/fomod/plugindependency.h
        src/core/fomod/plugingroup.h
        src/core/fomod/plugintype.h
        src/core/importmodinfo.h
        src/core/installer.cpp
        src/core/installer.h
        src/core/log.cpp
        src/core/log.h
        src/core/bsaarchive.cpp
        src/core/bsaarchive.h
        src/core/lspakextractor.cpp
        src/core/lspakextractor.h
        src/core/lspakfilelistentry.h
        src/core/lspakheader.h
        src/core/manualtag.cpp
        src/core/manualtag.h
        src/core/mod.cpp
        src/core/mod.h
        src/core/moddedapplication.cpp
        src/core/moddedapplication.h
        src/core/modrule.h
        src/core/modinfo.h
        src/core/nexus/api.cpp
        src/core/nexus/api.h
        src/core/nexus/collection.cpp
        src/core/nexus/collection.h
        src/core/nexus/file.cpp
        src/core/nexus/file.h
        src/core/nexus/integrityverifier.cpp
        src/core/nexus/integrityverifier.h
        src/core/nexus/mod.cpp
        src/core/nexus/mod.h
        src/core/parseerror.h
        src/core/pathutils.cpp
        src/core/pathutils.h
        src/core/plugindeployer.cpp
        src/core/plugindeployer.h
        src/core/progressnode.cpp
        src/core/progressnode.h
        src/core/remote/ommrepository.cpp
        src/core/remote/ommrepository.h
        src/core/remote/linkimporter.cpp
        src/core/remote/linkimporter.h
        src/core/remote/fsservermods.cpp
        src/core/remote/fsservermods.h
        src/core/reversedeployer.cpp
        src/core/reversedeployer.h
        src/core/tag.cpp
        src/core/tag.h
        src/core/tagcondition.h
        src/core/tagconditionnode.cpp
        src/core/tagconditionnode.h
        src/core/tool.cpp
        src/core/tool.h
        src/core/versionchangelog.cpp
        src/core/versionchangelog.h
        src/core/wildcardmatching.cpp
        src/core/wildcardmatching.h
        src/core/treeitem.t.hpp
        src/core/treeitem.h
        src/core/deployerentry.hpp
        src/core/deployerentry.cpp
        src/core/savemanager.cpp
        src/core/savemanager.h
        src/core/inimanager.cpp
        src/core/inimanager.h
)

# The LOOT/OpenMW deployers depend on libloot and are only built on demand.
if(LIMO_WITH_LOOT)
    list(APPEND CORE_SOURCES
            src/core/lootdeployer.cpp
            src/core/lootdeployer.h
            src/core/openmwarchivedeployer.cpp
            src/core/openmwarchivedeployer.h
            src/core/openmwplugindeployer.cpp
            src/core/openmwplugindeployer.h)
endif()

add_library(core OBJECT ${CORE_SOURCES})

if(LIMO_WITH_LOOT)
    target_compile_definitions(core PUBLIC LIMO_WITH_LOOT)
endif()

if(LIMO_WITH_UNRAR)
    target_compile_definitions(core PUBLIC LIMO_WITH_UNRAR)
endif()

target_include_directories(core
    PRIVATE ${PROJECT_SOURCE_DIR}/src
    PUBLIC ${LibArchive_INCLUDE_DIRS}
    PUBLIC ${LIBLOOT_INCLUDE_DIR}
    PUBLIC ${JSONCPP_INCLUDE_DIRS}
    PUBLIC ${LIBUNRAR_INCLUDE_DIR}
    PUBLIC ${LZ4_INCLUDE_DIRS}
    PUBLIC ${ZSTD_INCLUDE_DIRS})

target_link_libraries(core
    PUBLIC ${JSONCPP_LIBRARIES}
    PUBLIC ${LibArchive_LIBRARIES}
    PUBLIC ${LIBLOOT_PATH}
    PUBLIC cpr::cpr
    PUBLIC OpenSSL::SSL
    PUBLIC ${LIBUNRAR_PATH}
    PUBLIC ${LZ4_LIBRARIES}
    PUBLIC ${ZSTD_LIBRARIES}
    PUBLIC pugixml::pugixml
    PUBLIC ZLIB::ZLIB)

set(PROJECT_SOURCES
        resources/icons.qrc
        src/main.cpp
        src/ui/addapikeydialog.cpp
        src/ui/addapikeydialog.h
        src/ui/addapikeydialog.ui
        src/ui/addappdialog.cpp
        src/ui/addappdialog.h
        src/ui/addappdialog.ui
        src/ui/addautotagdialog.cpp
        src/ui/addautotagdialog.h
        src/ui/addautotagdialog.ui
        src/ui/addbackupdialog.cpp
        src/ui/addbackupdialog.h
        src/ui/addbackupdialog.ui
        src/ui/addbackuptargetdialog.cpp
        src/ui/addbackuptargetdialog.h
        src/ui/addbackuptargetdialog.ui
        src/ui/adddeployerdialog.cpp
        src/ui/adddeployerdialog.h
        src/ui/adddeployerdialog.ui
        src/ui/addmoddialog.cpp
        src/ui/addmoddialog.h
        src/ui/addmoddialog.ui
        src/ui/addprofiledialog.cpp
        src/ui/addprofiledialog.h
        src/ui/addprofiledialog.ui
        src/ui/addtodeployerdialog.cpp
        src/ui/addtodeployerdialog.h
        src/ui/addtodeployerdialog.ui
        src/ui/addtogroupdialog.cpp
        src/ui/addtogroupdialog.h
        src/ui/addtogroupdialog.ui
        src/ui/addtooldialog.cpp
        src/ui/addtooldialog.h
        src/ui/addtooldialog.ui
        src/ui/applicationmanager.cpp
        src/ui/applicationmanager.h
        src/ui/backuplistmodel.cpp
        src/ui/backuplistmodel.h
        src/ui/backuplistview.cpp
        src/ui/backuplistview.h
        src/ui/backupnamedelegate.cpp
        src/ui/backupnamedelegate.h
        src/ui/changeapipwdialog.cpp
        src/ui/changeapipwdialog.h
        src/ui/changeapipwdialog.ui
        src/ui/changelogdialog.cpp
        src/ui/changelogdialog.h
        src/ui/changelogdialog.ui
        # fork #197: Wabbajack .wabbajack modlist importer dialog
        src/ui/wabbajackimportdialog.cpp
        src/ui/wabbajackimportdialog.h
        src/ui/wabbajackimportdialog.ui
        # fork #211: in-app Nexus news/announcements feed
        src/ui/nexusnewsdialog.cpp
        src/ui/nexusnewsdialog.h
        src/ui/nexusnewsdialog.ui
        # fork #202: plugin ESM/ESL flag awareness
        src/ui/pluginflagsdialog.cpp
        src/ui/pluginflagsdialog.h
        src/ui/pluginflagsdialog.ui
        # fork #54: deploy restore points (load-order snapshots)
        src/ui/restorepointsdialog.cpp
        src/ui/restorepointsdialog.h
        src/ui/restorepointsdialog.ui
        # fork #145: bulk prune of outdated mod archive versions
        src/ui/pruneversionsdialog.cpp
        src/ui/pruneversionsdialog.h
        src/ui/pruneversionsdialog.ui
        src/ui/modconfigeditordialog.cpp
        src/ui/modconfigeditordialog.h
        src/ui/modconfigeditordialog.ui
        # fork #209: built-in game-asset preview for staged mod files
        src/ui/assetpreviewdialog.cpp
        src/ui/assetpreviewdialog.h
        src/ui/assetpreviewdialog.ui
        src/ui/collectiondialog.cpp
        src/ui/collectiondialog.h
        src/ui/collectiondialog.ui
        src/ui/nexuschangelogdialog.cpp
        src/ui/nexuschangelogdialog.h
        src/ui/nexuschangelogdialog.ui
        src/ui/colors.h
        src/ui/conflictdetaildialog.cpp
        src/ui/conflictdetaildialog.h
        src/ui/conflictdetaildialog.ui
        src/ui/conflictsmodel.cpp
        src/ui/conflictsmodel.h
        src/ui/deployerlistmodel.cpp
        src/ui/deployerlistmodel.h
        src/ui/deployerlistproxymodel.cpp
        src/ui/deployerlistproxymodel.h
        src/ui/deployerlistview.cpp
        src/ui/deployerlistview.h
        src/ui/deployedfilestreedialog.cpp
        src/ui/deployedfilestreedialog.h
        src/ui/deployedfilestreedialog.ui
        src/ui/bsabrowserdialog.cpp
        src/ui/bsabrowserdialog.h
        src/ui/bsabrowserdialog.ui
        src/ui/deployverifydialog.cpp
        src/ui/deployverifydialog.h
        src/ui/deployverifydialog.ui
        src/ui/healthcheckdialog.cpp
        src/ui/healthcheckdialog.h
        src/ui/healthcheckdialog.ui
        src/ui/instancedashboarddialog.cpp
        src/ui/instancedashboarddialog.h
        src/ui/instancedashboarddialog.ui
        src/ui/editautotagsdialog.cpp
        src/ui/editautotagsdialog.h
        src/ui/editautotagsdialog.ui
        src/ui/editmanualtagsdialog.cpp
        src/ui/editmanualtagsdialog.h
        src/ui/editmanualtagsdialog.ui
        src/ui/editmodsourcesdialog.cpp
        src/ui/editmodsourcesdialog.h
        src/ui/editmodsourcesdialog.ui
        src/ui/edittoolwidget.cpp
        src/ui/edittoolwidget.h
        src/ui/enterapipwdialog.cpp
        src/ui/enterapipwdialog.h
        src/ui/enterapipwdialog.ui
        src/ui/exportappconfigdialog.cpp
        src/ui/exportappconfigdialog.h
        src/ui/exportappconfigdialog.ui
        src/ui/externalchangesdialog.cpp
        src/ui/externalchangesdialog.h
        src/ui/externalchangesdialog.ui
        src/ui/fomodcheckbox.cpp
        src/ui/fomodcheckbox.h
        src/ui/fomoddialog.cpp
        src/ui/fomoddialog.h
        src/ui/fomoddialog.ui
        src/ui/fomodradiobutton.cpp
        src/ui/fomodradiobutton.h
        src/ui/fsservermodsdialog.cpp
        src/ui/fsservermodsdialog.h
        src/ui/importfromsteamdialog.cpp
        src/ui/importfromsteamdialog.h
        src/ui/importfromsteamdialog.ui
        src/ui/importmo2dialog.cpp
        src/ui/importmo2dialog.h
        src/ui/importmo2dialog.ui
        src/ui/ipcclient.cpp
        src/ui/ipcclient.h
        src/ui/ipcserver.cpp
        src/ui/ipcserver.h
        src/ui/listaction.cpp
        src/ui/listaction.h
        src/ui/mainwindow.cpp
        src/ui/mainwindow.h
        src/ui/mainwindow.ui
        src/ui/managegroupsdialog.cpp
        src/ui/managegroupsdialog.h
        src/ui/managegroupsdialog.ui
        src/ui/managemodtagsdialog.cpp
        src/ui/managemodtagsdialog.h
        src/ui/managemodtagsdialog.ui
        src/ui/managemodrulesdialog.cpp
        src/ui/managemodrulesdialog.h
        src/ui/managemodrulesdialog.ui
        src/ui/modlistmodel.cpp
        src/ui/modlistmodel.h
        src/ui/modlistproxymodel.cpp
        src/ui/modlistproxymodel.h
        src/ui/modlistview.cpp
        src/ui/modlistview.h
        src/ui/modpacksdialog.cpp
        src/ui/modpacksdialog.h
        src/ui/modnamedelegate.cpp
        src/ui/modnamedelegate.h
        src/ui/movemoddialog.cpp
        src/ui/movemoddialog.h
        src/ui/movemoddialog.ui
        src/ui/nexusmoddialog.cpp
        src/ui/nexusmoddialog.h
        src/ui/nexusmoddialog.ui
        src/ui/nexusbrowserdialog.cpp
        src/ui/nexusbrowserdialog.h
        src/ui/nexusbrowserdialog.ui
        src/ui/overwritebackupdialog.cpp
        src/ui/overwritebackupdialog.h
        src/ui/overwritebackupdialog.ui
        src/ui/passwordfield.cpp
        src/ui/passwordfield.h
        src/ui/repositoriesdialog.cpp
        src/ui/repositoriesdialog.h
        src/ui/repositoriesdialog.ui
        src/ui/rootlevelcondition.cpp
        src/ui/rootlevelcondition.h
        src/ui/settingsdialog.cpp
        src/ui/settingsdialog.h
        src/ui/settingsdialog.ui
        src/ui/tablecelldelegate.cpp
        src/ui/tablecelldelegate.h
        src/ui/tablepushbutton.cpp
        src/ui/tablepushbutton.h
        src/ui/tabletoolbutton.cpp
        src/ui/tabletoolbutton.h
        src/ui/tagcheckbox.cpp
        src/ui/tagcheckbox.h
        src/ui/validatinglineedit.cpp
        src/ui/validatinglineedit.h
        src/ui/versionboxdelegate.cpp
        src/ui/versionboxdelegate.h
        src/ui/qmodelindexutils.h
        # fork #8: persistent download queue with progress and retry
        src/ui/downloadswidget.cpp
        src/ui/downloadswidget.h
        src/ui/downloadswidget.ui
        # Deploy dry-run / preview dialog (fork feature #49).
        src/ui/deploypreviewdialog.cpp
        src/ui/deploypreviewdialog.h
        src/ui/deploypreviewdialog.ui
        # fork #24: save-game manager tab
        src/ui/savemanagerwidget.cpp
        src/ui/savemanagerwidget.h
        src/ui/savemanagerwidget.ui
        # fork #149: load-order bisect tool
        src/ui/loadorderbisectdialog.cpp
        src/ui/loadorderbisectdialog.h
        src/ui/loadorderbisectdialog.ui
)

qt_add_resources(QSS_RESOURCES
    styles/styles.qrc
)

# The LOOT user-metadata editor pulls in lootdeployer.h (loot/api.h), so it is only
# built when LIMO_WITH_LOOT is enabled (mirrors the core LOOT deployer gating).
if(LIMO_WITH_LOOT)
    list(APPEND PROJECT_SOURCES
            src/ui/lootuserlistdialog.cpp
            src/ui/lootuserlistdialog.h
            src/ui/lootuserlistdialog.ui)
endif()

# Set install target so app config files can be found
if(NOT DEFINED LIMO_INSTALL_PREFIX)
  set(LIMO_INSTALL_PREFIX "/usr/local")
endif()

configure_file(src/core/consts.h.in
               ${PROJECT_SOURCE_DIR}/src/core/consts.h)

add_executable(Limo
    ${PROJECT_SOURCES}
    ${QSS_RESOURCES}
)

target_include_directories(Limo PRIVATE ${PROJECT_SOURCE_DIR}/src)

target_link_libraries(Limo
    PRIVATE core
    PRIVATE Qt${QT_VERSION_MAJOR}::Widgets
    PRIVATE Qt${QT_VERSION_MAJOR}::Svg
    PRIVATE Qt${QT_VERSION_MAJOR}::Network)

install(PROGRAMS ${CMAKE_BINARY_DIR}/Limo
    DESTINATION bin RENAME limo)

if(IS_FLATPAK)
  install(FILES flatpak/io.github.limo_app.limo.png
            DESTINATION /app/share/icons/hicolor/512x512/apps)
  install(FILES flatpak/io.github.limo_app.limo.metainfo.xml
            DESTINATION /app/share/metainfo)
  install(FILES flatpak/io.github.limo_app.limo.desktop
            DESTINATION /app/share/applications)
  install(DIRECTORY steam_app_configs
            DESTINATION /app/share/limo)
  install(FILES install_files/changelogs.json
            DESTINATION /app/share/limo)
else()
  install(DIRECTORY steam_app_configs
            DESTINATION ${LIMO_INSTALL_PREFIX}/share/limo)
  install(FILES install_files/limo.png
            DESTINATION ${LIMO_INSTALL_PREFIX}/share/icons/hicolor/512x512/apps)
  install(FILES install_files/limo.desktop
            DESTINATION ${LIMO_INSTALL_PREFIX}/share/applications)
  install(FILES flatpak/io.github.limo_app.limo.metainfo.xml
            DESTINATION ${LIMO_INSTALL_PREFIX}/share/metainfo)
  install(FILES install_files/changelogs.json
            DESTINATION ${LIMO_INSTALL_PREFIX}/share/limo)
endif()

# fork #22: Qt translation (i18n) support.
# The .ts sources live in translations/. Compiling them into .qm requires the
# Qt LinguistTools component, which is OPTIONAL: when it (or lrelease) is missing
# the build proceeds unchanged and the app silently falls back to English. When
# available, the generated .qm files are installed to share/limo/translations,
# where main.cpp looks for them at runtime.
set(LIMO_TS_FILES
    ${PROJECT_SOURCE_DIR}/translations/limo_en.ts)

if(Qt${QT_VERSION_MAJOR}LinguistTools_FOUND)
  if(IS_FLATPAK)
    set(LIMO_QM_INSTALL_DIR /app/share/limo/translations)
  else()
    set(LIMO_QM_INSTALL_DIR ${LIMO_INSTALL_PREFIX}/share/limo/translations)
  endif()

  if(COMMAND qt_add_translations)
    # Qt >= 6.2 convenience API: builds the .qm files and ties them to the target.
    qt_add_translations(Limo
        TS_FILES ${LIMO_TS_FILES}
        QM_FILES_OUTPUT_VARIABLE LIMO_QM_FILES)
    install(FILES ${LIMO_QM_FILES} DESTINATION ${LIMO_QM_INSTALL_DIR})
  else()
    # Fallback: run lrelease directly, guarded behind the LinguistTools check.
    qt_add_lrelease(LIMO_QM_FILES TS_FILES ${LIMO_TS_FILES})
    add_custom_target(limo_translations ALL DEPENDS ${LIMO_QM_FILES})
    add_dependencies(Limo limo_translations)
    install(FILES ${LIMO_QM_FILES} DESTINATION ${LIMO_QM_INSTALL_DIR})
  endif()
else()
  message(STATUS
    "Qt LinguistTools not found: skipping .qm translation generation (fork #22). "
    "The UI will use the English source strings.")
endif()

# Tests are built when LIMO_BUILD_TESTS is set (or the standard BUILD_TESTING flag).
if(LIMO_BUILD_TESTS OR BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()
