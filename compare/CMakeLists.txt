# compare/CMakeLists.txt --- RC compare module (static library)
##############################################################################

add_library(libRcCompare STATIC
    CompareEngine.cpp
    MergeEngine.cpp
    TopologyMatcher.cpp
    ResourceManager.cpp
    ViewRenderer.cpp
    NavigationTree.cpp
    CRcCompareControler.cpp
    CCompareWorkerThread.cpp
    CompareProjectConfig.cpp
    LevelManagerJsonHelper.cpp
    CFormRcBase.cpp
    CFormRcCompare.cpp
    CFileDropListCtrl.cpp
    CConflictResolveDialog.cpp
    CTaskEditDialog.cpp
    CRcCompareViewer.cpp
    ${CMAKE_SOURCE_DIR}/src/MSplitterWnd.cpp)

set_target_properties(libRcCompare PROPERTIES PREFIX "")

target_include_directories(libRcCompare PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/MyWndCtrl
    ${CMAKE_SOURCE_DIR}/WonSetThreadUILanguage
    ${CMAKE_SOURCE_DIR}/EGA
    ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_definitions(libRcCompare PRIVATE -DUNICODE -D_UNICODE)

##############################################################################
# RcCompare.exe --- Standalone compare tool

add_executable(RcCompare WIN32
    RcCompare.cpp
    RcCompare_res.rc
    res/RcCompare.manifest)

target_include_directories(RcCompare PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/MyWndCtrl
    ${CMAKE_SOURCE_DIR}/WonSetThreadUILanguage
    ${CMAKE_SOURCE_DIR}/EGA
    ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(RcCompare
    libRcCompare libRisohEditorCore libega
    gdi32 comctl32 advapi32 shlwapi ole32 shell32 comdlg32 version gdiplus)

target_compile_definitions(RcCompare PRIVATE -DUNICODE -D_UNICODE)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set_target_properties(RcCompare PROPERTIES OUTPUT_NAME "RcCompare64")
endif()
