shared {
    DEFINES += UTILS_LIBRARY
} else {
    DEFINES += QTCREATOR_UTILS_STATIC_LIB
}

QT += widgets gui network qml xml
greaterThan(QT_MAJOR_VERSION, 5): QT += core5compat


CONFIG += exceptions # used by portlist.cpp, textfileformat.cpp, and ssh/*

win32: LIBS += -luser32 -lshell32
# PortsGatherer
win32: LIBS += -liphlpapi -lws2_32

SOURCES += \
    $$PWD/globalfilechangeblocker.cpp \
    $$PWD/benchmarker.cpp \
    $$PWD/displayname.cpp \
    $$PWD/environment.cpp \
    $$PWD/environmentmodel.cpp \
    $$PWD/environmentdialog.cpp \
    $$PWD/namevaluedictionary.cpp \
    $$PWD/namevalueitem.cpp \
    $$PWD/namevaluemodel.cpp \
    $$PWD/namevaluesdialog.cpp \
    $$PWD/commandline.cpp \
    $$PWD/qrcparser.cpp \
    $$PWD/qtcprocess.cpp \
    $$PWD/reloadpromptutils.cpp \
    $$PWD/settingsaccessor.cpp \
    $$PWD/shellcommand.cpp \
    $$PWD/shellcommandpage.cpp \
    $$PWD/settingsselector.cpp \
    $$PWD/stringutils.cpp \
    $$PWD/templateengine.cpp \
    $$PWD/temporarydirectory.cpp \
    $$PWD/temporaryfile.cpp \
    $$PWD/textfieldcheckbox.cpp \
    $$PWD/textfieldcombobox.cpp \
    $$PWD/filesearch.cpp \
    $$PWD/pathchooser.cpp \
    $$PWD/pathlisteditor.cpp \
    $$PWD/wizard.cpp \
    $$PWD/wizardpage.cpp \
    $$PWD/filewizardpage.cpp \
    $$PWD/filesystemwatcher.cpp \
    $$PWD/projectintropage.cpp \
    $$PWD/filenamevalidatinglineedit.cpp \
    $$PWD/codegeneration.cpp \
    $$PWD/classnamevalidatinglineedit.cpp \
    $$PWD/fancylineedit.cpp \
    $$PWD/qtcolorbutton.cpp \
    $$PWD/savefile.cpp \
    $$PWD/filepath.cpp \
    $$PWD/fileutils.cpp \
    $$PWD/textfileformat.cpp \
    $$PWD/consoleprocess.cpp \
    $$PWD/uncommentselection.cpp \
    $$PWD/parameteraction.cpp \
    $$PWD/headerviewstretcher.cpp \
    $$PWD/checkablemessagebox.cpp \
    $$PWD/styledbar.cpp \
    $$PWD/stylehelper.cpp \
    $$PWD/fancymainwindow.cpp \
    $$PWD/detailsbutton.cpp \
    $$PWD/detailswidget.cpp \
    $$PWD/changeset.cpp \
    $$PWD/faketooltip.cpp \
    $$PWD/htmldocextractor.cpp \
    $$PWD/navigationtreeview.cpp \
    $$PWD/crumblepath.cpp \
    $$PWD/historycompleter.cpp \
    $$PWD/buildablehelperlibrary.cpp \
    $$PWD/delegates.cpp \
    $$PWD/fileinprojectfinder.cpp \
    $$PWD/statuslabel.cpp \
    $$PWD/outputformatter.cpp \
    $$PWD/flowlayout.cpp \
    $$PWD/networkaccessmanager.cpp \
    $$PWD/persistentsettings.cpp \
    $$PWD/completingtextedit.cpp \
    $$PWD/json.cpp \
    $$PWD/portlist.cpp \
    $$PWD/processhandle.cpp \
    $$PWD/processutils.cpp \
    $$PWD/appmainwindow.cpp \
    $$PWD/basetreeview.cpp \
    $$PWD/qtcassert.cpp \
    $$PWD/elfreader.cpp \
    $$PWD/proxyaction.cpp \
    $$PWD/elidinglabel.cpp \
    $$PWD/hostosinfo.cpp \
    $$PWD/tooltip/tooltip.cpp \
    $$PWD/tooltip/tips.cpp \
    $$PWD/unixutils.cpp \
    $$PWD/ansiescapecodehandler.cpp \
    $$PWD/execmenu.cpp \
    $$PWD/completinglineedit.cpp \
    $$PWD/winutils.cpp \
    $$PWD/itemviews.cpp \
    $$PWD/treemodel.cpp \
    $$PWD/treeviewcombobox.cpp \
    $$PWD/proxycredentialsdialog.cpp \
    $$PWD/macroexpander.cpp \
    $$PWD/theme/theme.cpp \
    $$PWD/progressindicator.cpp \
    $$PWD/fadingindicator.cpp \
    $$PWD/overridecursor.cpp \
    $$PWD/categorysortfiltermodel.cpp \
    $$PWD/dropsupport.cpp \
    $$PWD/icon.cpp \
    $$PWD/port.cpp \
    $$PWD/runextensions.cpp \
    $$PWD/utilsicons.cpp \
    $$PWD/guard.cpp \
    $$PWD/highlightingitemdelegate.cpp \
    $$PWD/fuzzymatcher.cpp \
    $$PWD/textutils.cpp \
    $$PWD/url.cpp \
    $$PWD/filecrumblabel.cpp \
    $$PWD/fixedsizeclicklabel.cpp \
    $$PWD/removefiledialog.cpp \
    $$PWD/differ.cpp \
    $$PWD/jsontreeitem.cpp \
    $$PWD/namevaluevalidator.cpp \
    $$PWD/camelcasecursor.cpp \
    $$PWD/infolabel.cpp \
    $$PWD/overlaywidget.cpp \
    $$PWD/archive.cpp \
    $$PWD/id.cpp \
    $$PWD/infobar.cpp \
    $$PWD/aspects.cpp \
    $$PWD/layoutbuilder.cpp \
    $$PWD/variablechooser.cpp \
    $$PWD/futuresynchronizer.cpp \
    $$PWD/launcherinterface.cpp \
    $$PWD/launcherpackets.cpp \
    $$PWD/launchersocket.cpp \
    $$PWD/qtcsettings.cpp \
    $$PWD/link.cpp \
    $$PWD/linecolumn.cpp \

HEADERS += \
    $$PWD/environmentfwd.h \
    $$PWD/genericconstants.h \
    $$PWD/globalfilechangeblocker.h \
    $$PWD/indexedcontainerproxyconstiterator.h \
    $$PWD/benchmarker.h \
    $$PWD/displayname.h \
    $$PWD/environment.h \
    $$PWD/environmentmodel.h \
    $$PWD/environmentdialog.h \
    $$PWD/namevaluedictionary.h \
    $$PWD/namevalueitem.h \
    $$PWD/namevaluemodel.h \
    $$PWD/namevaluesdialog.h \
    $$PWD/commandline.h \
    $$PWD/qrcparser.h \
    $$PWD/qtcprocess.h \
    $$PWD/span.h \
    $$PWD/../3rdparty/span/span.hpp \
    $$PWD/utils_global.h \
    $$PWD/reloadpromptutils.h \
    $$PWD/settingsaccessor.h \
    $$PWD/settingsselector.h \
    $$PWD/shellcommand.h \
    $$PWD/shellcommandpage.h \
    $$PWD/stringutils.h \
    $$PWD/templateengine.h \
    $$PWD/temporarydirectory.h \
    $$PWD/temporaryfile.h \
    $$PWD/textfieldcheckbox.h \
    $$PWD/textfieldcombobox.h \
    $$PWD/filesearch.h \
    $$PWD/listutils.h \
    $$PWD/pathchooser.h \
    $$PWD/pathlisteditor.h \
    $$PWD/wizard.h \
    $$PWD/wizardpage.h \
    $$PWD/filewizardpage.h \
    $$PWD/filesystemwatcher.h \
    $$PWD/projectintropage.h \
    $$PWD/filenamevalidatinglineedit.h \
    $$PWD/codegeneration.h \
    $$PWD/classnamevalidatinglineedit.h \
    $$PWD/fancylineedit.h \
    $$PWD/qtcolorbutton.h \
    $$PWD/consoleprocess.h \
    $$PWD/savefile.h \
    $$PWD/filepath.h \
    $$PWD/fileutils.h \
    $$PWD/textfileformat.h \
    $$PWD/uncommentselection.h \
    $$PWD/parameteraction.h \
    $$PWD/headerviewstretcher.h \
    $$PWD/checkablemessagebox.h \
    $$PWD/qtcassert.h \
    $$PWD/styledbar.h \
    $$PWD/stylehelper.h \
    $$PWD/fancymainwindow.h \
    $$PWD/detailsbutton.h \
    $$PWD/detailswidget.h \
    $$PWD/changeset.h \
    $$PWD/faketooltip.h \
    $$PWD/htmldocextractor.h \
    $$PWD/navigationtreeview.h \
    $$PWD/crumblepath.h \
    $$PWD/historycompleter.h \
    $$PWD/buildablehelperlibrary.h \
    $$PWD/delegates.h \
    $$PWD/fileinprojectfinder.h \
    $$PWD/statuslabel.h \
    $$PWD/outputformatter.h \
    $$PWD/outputformat.h \
    $$PWD/flowlayout.h \
    $$PWD/networkaccessmanager.h \
    $$PWD/persistentsettings.h \
    $$PWD/completingtextedit.h \
    $$PWD/json.h \
    $$PWD/runextensions.h \
    $$PWD/portlist.h \
    $$PWD/processhandle.h \
    $$PWD/processutils.h \
    $$PWD/appmainwindow.h \
    $$PWD/basetreeview.h \
    $$PWD/elfreader.h \
    $$PWD/proxyaction.h \
    $$PWD/hostosinfo.h \
    $$PWD/osspecificaspects.h \
    $$PWD/elidinglabel.h \
    $$PWD/tooltip/tooltip.h \
    $$PWD/tooltip/tips.h \
    $$PWD/tooltip/effects.h \
    $$PWD/unixutils.h \
    $$PWD/ansiescapecodehandler.h \
    $$PWD/execmenu.h \
    $$PWD/completinglineedit.h \
    $$PWD/winutils.h \
    $$PWD/itemviews.h \
    $$PWD/treemodel.h \
    $$PWD/treeviewcombobox.h \
    $$PWD/scopedswap.h \
    $$PWD/algorithm.h \
    $$PWD/QtConcurrentTools \
    $$PWD/proxycredentialsdialog.h \
    $$PWD/macroexpander.h \
    $$PWD/theme/theme.h \
    $$PWD/theme/theme_p.h \
    $$PWD/progressindicator.h \
    $$PWD/fadingindicator.h \
    $$PWD/executeondestruction.h \
    $$PWD/overridecursor.h \
    $$PWD/categorysortfiltermodel.h \
    $$PWD/dropsupport.h \
    $$PWD/utilsicons.h \
    $$PWD/icon.h \
    $$PWD/port.h \
    $$PWD/functiontraits.h \
    $$PWD/mapreduce.h \
    $$PWD/declarationmacros.h \
    $$PWD/smallstring.h \
    $$PWD/smallstringiterator.h \
    $$PWD/smallstringliteral.h \
    $$PWD/smallstringmemory.h \
    $$PWD/smallstringvector.h \
    $$PWD/smallstringlayout.h \
    $$PWD/sizedarray.h \
    $$PWD/smallstringio.h \
    $$PWD/guard.h \
    $$PWD/smallstringfwd.h \
    $$PWD/optional.h \
    $$PWD/../3rdparty/optional/optional.hpp \
    $$PWD/variant.h \
    $$PWD/../3rdparty/variant/variant.hpp \
    $$PWD/highlightingitemdelegate.h \
    $$PWD/fuzzymatcher.h \
    $$PWD/textutils.h \
    $$PWD/predicates.h \
    $$PWD/url.h \
    $$PWD/filecrumblabel.h \
    $$PWD/linecolumn.h \
    $$PWD/link.h \
    $$PWD/fixedsizeclicklabel.h \
    $$PWD/removefiledialog.h \
    $$PWD/differ.h \
    $$PWD/cpplanguage_details.h \
    $$PWD/jsontreeitem.h \
    $$PWD/listmodel.h \
    $$PWD/namevaluevalidator.h \
    $$PWD/camelcasecursor.h \
    $$PWD/infolabel.h \
    $$PWD/overlaywidget.h \
    $$PWD/archive.h \
    $$PWD/id.h \
    $$PWD/infobar.h \
    $$PWD/porting.h \
    $$PWD/aspects.h \
    $$PWD/layoutbuilder.h \
    $$PWD/variablechooser.h \
    $$PWD/set_algorithm.h \
    $$PWD/futuresynchronizer.h \
    $$PWD/launcherinterface.h \
    $$PWD/launcherpackets.h \
    $$PWD/launchersocket.h \
    $$PWD/qtcsettings.h

FORMS += $$PWD/filewizardpage.ui \
    $$PWD/projectintropage.ui \
    $$PWD/proxycredentialsdialog.ui \
    $$PWD/removefiledialog.ui

RESOURCES += $$PWD/utils.qrc

osx {
    HEADERS += \
        $$PWD/theme/theme_mac.h \
        $$PWD/fileutils_mac.h

    OBJECTIVE_SOURCES += \
        $$PWD/theme/theme_mac.mm \
        $$PWD/fileutils_mac.mm \
        $$PWD/processhandle_mac.mm

    LIBS += -framework Foundation -framework AppKit
}

include(touchbar/touchbar.pri)
include(mimetypes/mimetypes.pri)
