DEFINES += CPPEDITOR_LIBRARY
include(../../qtcreatorplugin.pri)

HEADERS += \
    cppautocompleter.h \
    cppcodemodelinspectordialog.h \
    cppdocumentationcommenthelper.h \
    cppeditor.h \
    cppeditor_global.h \
    cppeditordocument.h \
    cppeditorconstants.h \
    cppeditorenums.h \
    cppeditorplugin.h \
    cppeditorwidget.h \
    cppfunctiondecldeflink.h \
    cpphighlighter.h \
    cppparsecontext.h \
    cppincludehierarchy.h \
    cppinsertvirtualmethods.h \
    cpplocalrenaming.h \
    cppminimizableinfobars.h \
    cppoutline.h \
    cpppreprocessordialog.h \
    cppquickfix.h \
    cppquickfixassistant.h \
    cppquickfixes.h \
    cppquickfixprojectsettings.h \
    cppquickfixprojectsettingswidget.h \
    cppquickfixsettings.h \
    cppquickfixsettingspage.h \
    cppquickfixsettingswidget.h \
    cpptypehierarchy.h \
    cppuseselectionsupdater.h \
    resourcepreviewhoverhandler.h

SOURCES += \
    cppautocompleter.cpp \
    cppcodemodelinspectordialog.cpp \
    cppdocumentationcommenthelper.cpp \
    cppeditor.cpp \
    cppeditordocument.cpp \
    cppeditorplugin.cpp \
    cppeditorwidget.cpp \
    cppfunctiondecldeflink.cpp \
    cpphighlighter.cpp \
    cppparsecontext.cpp \
    cppincludehierarchy.cpp \
    cppinsertvirtualmethods.cpp \
    cpplocalrenaming.cpp \
    cppminimizableinfobars.cpp \
    cppoutline.cpp \
    cpppreprocessordialog.cpp \
    cppquickfix.cpp \
    cppquickfixassistant.cpp \
    cppquickfixes.cpp \
    cppquickfixprojectsettings.cpp \
    cppquickfixprojectsettingswidget.cpp \
    cppquickfixsettings.cpp \
    cppquickfixsettingspage.cpp \
    cppquickfixsettingswidget.cpp \
    cpptypehierarchy.cpp \
    cppuseselectionsupdater.cpp \
    resourcepreviewhoverhandler.cpp

FORMS += \
    cpppreprocessordialog.ui \
    cppcodemodelinspectordialog.ui \
    cppquickfixprojectsettingswidget.ui \
    cppquickfixsettingswidget.ui

RESOURCES += \
    cppeditor.qrc

equals(TEST, 1) {
    HEADERS += \
        cppeditortestcase.h \
        cppdoxygen_test.h \
        cppincludehierarchy_test.h \
        cppquickfix_test.h \
        cppuseselections_test.h \
        fileandtokenactions_test.h \
        followsymbol_switchmethoddecldef_test.h

    SOURCES += \
        cppdoxygen_test.cpp \
        cppeditortestcase.cpp \
        cppincludehierarchy_test.cpp \
        cppquickfix_test.cpp \
        cppuseselections_test.cpp \
        fileandtokenactions_test.cpp \
        followsymbol_switchmethoddecldef_test.cpp
    DEFINES += SRCDIR=\\\"$$PWD\\\"
}
