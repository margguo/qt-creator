/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of Qt Creator.
**
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3 as published by the Free Software
** Foundation with exceptions as appearing in the file LICENSE.GPL3-EXCEPT
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-3.0.html.
**
****************************************************************************/

#include "qmakeprojectimporter.h"

#include "qmakebuildinfo.h"
#include "qmakekitinformation.h"
#include "qmakebuildconfiguration.h"
#include "qmakeproject.h"
#include "makefileparse.h"
#include "qmakestep.h"

#include <projectexplorer/buildinfo.h>
#include <projectexplorer/projectexplorerconstants.h>
#include <projectexplorer/kitmanager.h>
#include <projectexplorer/toolchain.h>
#include <projectexplorer/toolchainmanager.h>

#include <qtsupport/qtkitinformation.h>
#include <qtsupport/qtsupportconstants.h>
#include <qtsupport/qtversionfactory.h>
#include <qtsupport/qtversionmanager.h>

#include <utils/algorithm.h>
#include <utils/qtcassert.h>
#include <utils/qtcprocess.h>

#include <QDir>
#include <QFileInfo>
#include <QStringList>
#include <QLoggingCategory>

#include <memory>

using namespace ProjectExplorer;
using namespace QmakeProjectManager;
using namespace QtSupport;
using namespace Utils;

namespace {

struct DirectoryData
{
    QString makefile;
    Utils::FilePath buildDirectory;
    Utils::FilePath canonicalQmakeBinary;
    QtProjectImporter::QtVersionData qtVersionData;
    QString parsedSpec;
    BaseQtVersion::QmakeBuildConfigs buildConfig;
    QString additionalArguments;
    QMakeStepConfig config;
    QMakeStepConfig::TargetArchConfig archConfig;
    QMakeStepConfig::OsType osType;
};

} // namespace

namespace QmakeProjectManager {
namespace Internal {

const Utils::Id QT_IS_TEMPORARY("Qmake.TempQt");
const char IOSQT[] = "Qt4ProjectManager.QtVersion.Ios"; // ugly

QmakeProjectImporter::QmakeProjectImporter(const FilePath &path) :
    QtProjectImporter(path)
{ }

QStringList QmakeProjectImporter::importCandidates()
{
    QStringList candidates;

    QFileInfo pfi = projectFilePath().toFileInfo();
    const QString prefix = pfi.baseName();
    candidates << pfi.absolutePath();

    foreach (Kit *k, KitManager::kits()) {
        const FilePath sbdir = QmakeBuildConfiguration::shadowBuildDirectory
                    (projectFilePath(), k, QString(), BuildConfiguration::Unknown);

        const QString baseDir = sbdir.toFileInfo().absolutePath();

        foreach (const QString &dir, QDir(baseDir).entryList()) {
            const QString path = baseDir + QLatin1Char('/') + dir;
            if (dir.startsWith(prefix) && !candidates.contains(path))
                candidates << path;
        }
    }
    return candidates;
}

QList<void *> QmakeProjectImporter::examineDirectory(const FilePath &importPath,
                                                     QString *warningMessage) const
{
    Q_UNUSED(warningMessage)
    QList<void *> result;
    const QLoggingCategory &logs = MakeFileParse::logging();

    QStringList makefiles = QDir(importPath.toString()).entryList(QStringList(QLatin1String("Makefile*")));
    qCDebug(logs) << "  Makefiles:" << makefiles;

    foreach (const QString &file, makefiles) {
        std::unique_ptr<DirectoryData> data(new DirectoryData);
        data->makefile = file;
        data->buildDirectory = importPath;

        qCDebug(logs) << "  Parsing makefile" << file;
        // find interesting makefiles
        const FilePath makefile = importPath / file;
        MakeFileParse parse(makefile, MakeFileParse::Mode::FilterKnownConfigValues);
        if (parse.makeFileState() != MakeFileParse::Okay) {
            qCDebug(logs) << "  Parsing the makefile failed" << makefile;
            continue;
        }
        if (parse.srcProFile() != projectFilePath()) {
            qCDebug(logs) << "  pro files doesn't match" << parse.srcProFile() << projectFilePath();
            continue;
        }

        data->canonicalQmakeBinary = parse.qmakePath().canonicalPath();
        if (data->canonicalQmakeBinary.isEmpty()) {
            qCDebug(logs) << "  " << parse.qmakePath() << "doesn't exist anymore";
            continue;
        }

        qCDebug(logs) << "  QMake:" << data->canonicalQmakeBinary;

        data->qtVersionData = QtProjectImporter::findOrCreateQtVersion(data->canonicalQmakeBinary);
        BaseQtVersion *version = data->qtVersionData.qt;
        bool isTemporaryVersion = data->qtVersionData.isTemporary;

        QTC_ASSERT(version, continue);

        qCDebug(logs) << "  qt version:" << version->displayName() << " temporary:" << isTemporaryVersion;

        data->archConfig = parse.config().archConfig;
        data->osType = parse.config().osType;

        qCDebug(logs) << "  archConfig:" << data->archConfig;
        qCDebug(logs) << "  osType:    " << data->osType;
        if (version->type() == QLatin1String(IOSQT)
                && data->osType == QMakeStepConfig::NoOsType) {
            data->osType = QMakeStepConfig::IphoneOS;
            qCDebug(logs) << "  IOS found without osType, adjusting osType" << data->osType;
        }

        if (version->type() == QtSupport::Constants::DESKTOPQT) {
            const ProjectExplorer::Abis abis = version->qtAbis();
            if (!abis.isEmpty()) {
                ProjectExplorer::Abi abi = abis.first();
                if (abi.os() == ProjectExplorer::Abi::DarwinOS) {
                    if (abi.wordWidth() == 64)
                        data->archConfig = QMakeStepConfig::X86_64;
                    else
                        data->archConfig = QMakeStepConfig::X86;
                    qCDebug(logs) << "  OS X found without targetarch, adjusting archType" << data->archConfig;
                }
            }
        }

        // find qmake arguments and mkspec
        data->additionalArguments = parse.unparsedArguments();
        qCDebug(logs) << "  Unparsed arguments:" << data->additionalArguments;
        data->parsedSpec =
                QmakeBuildConfiguration::extractSpecFromArguments(&(data->additionalArguments), importPath, version);
        qCDebug(logs) << "  Extracted spec:" << data->parsedSpec;
        qCDebug(logs) << "  Arguments now:" << data->additionalArguments;

        const QString versionSpec = version->mkspec();
        if (data->parsedSpec.isEmpty() || data->parsedSpec == "default") {
            data->parsedSpec = versionSpec;
            qCDebug(logs) << "  No parsed spec or default spec => parsed spec now:" << data->parsedSpec;
        }
        data->buildConfig = parse.effectiveBuildConfig(data->qtVersionData.qt->defaultBuildConfig());
        data->config = parse.config();

        result.append(data.release());
    }
    return result;
}

bool QmakeProjectImporter::matchKit(void *directoryData, const Kit *k) const
{
    auto *data = static_cast<DirectoryData *>(directoryData);
    const QLoggingCategory &logs = MakeFileParse::logging();

    BaseQtVersion *kitVersion = QtKitAspect::qtVersion(k);
    QString kitSpec = QmakeKitAspect::mkspec(k);
    ToolChain *tc = ToolChainKitAspect::cxxToolChain(k);
    if (kitSpec.isEmpty() && kitVersion)
        kitSpec = kitVersion->mkspecFor(tc);
    QMakeStepConfig::TargetArchConfig kitTargetArch = QMakeStepConfig::NoArch;
    QMakeStepConfig::OsType kitOsType = QMakeStepConfig::NoOsType;
    if (tc) {
        kitTargetArch = QMakeStepConfig::targetArchFor(tc->targetAbi(), kitVersion);
        kitOsType = QMakeStepConfig::osTypeFor(tc->targetAbi(), kitVersion);
    }
    qCDebug(logs) << k->displayName()
                  << "version:" << (kitVersion == data->qtVersionData.qt)
                  << "spec:" << (kitSpec == data->parsedSpec)
                  << "targetarch:" << (kitTargetArch == data->archConfig)
                  << "ostype:" << (kitOsType == data->osType);
    return kitVersion == data->qtVersionData.qt
            && kitSpec == data->parsedSpec
            && kitTargetArch == data->archConfig
            && kitOsType == data->osType;
}

Kit *QmakeProjectImporter::createKit(void *directoryData) const
{
    auto *data = static_cast<DirectoryData *>(directoryData);
    return createTemporaryKit(data->qtVersionData, data->parsedSpec, data->archConfig, data->osType);
}

const QList<BuildInfo> QmakeProjectImporter::buildInfoList(void *directoryData) const
{
    auto *data = static_cast<DirectoryData *>(directoryData);

    // create info:
    BuildInfo info;
    if (data->buildConfig & BaseQtVersion::DebugBuild) {
        info.buildType = BuildConfiguration::Debug;
        info.displayName = QCoreApplication::translate("QmakeProjectManager::Internal::QmakeProjectImporter", "Debug");
    } else {
        info.buildType = BuildConfiguration::Release;
        info.displayName = QCoreApplication::translate("QmakeProjectManager::Internal::QmakeProjectImporter", "Release");
    }
    info.buildDirectory = data->buildDirectory;

    QmakeExtraBuildInfo extra;
    extra.additionalArguments = data->additionalArguments;
    extra.config = data->config;
    extra.makefile = data->makefile;
    info.extraInfo = QVariant::fromValue(extra);

    return {info};
}

void QmakeProjectImporter::deleteDirectoryData(void *directoryData) const
{
    delete static_cast<DirectoryData *>(directoryData);
}

static const QList<ToolChain *> preferredToolChains(BaseQtVersion *qtVersion, const QString &ms,
                                                    const QMakeStepConfig::TargetArchConfig &archConfig)
{
    const QString spec = ms.isEmpty() ? qtVersion->mkspec() : ms;

    const QList<ToolChain *> toolchains = ToolChainManager::toolChains();
    const Abis qtAbis = qtVersion->qtAbis();
    const auto matcher = [&](const ToolChain *tc) {
        return qtAbis.contains(tc->targetAbi())
            && tc->suggestedMkspecList().contains(spec)
            && QMakeStepConfig::targetArchFor(tc->targetAbi(), qtVersion) == archConfig;
    };
    ToolChain * const cxxToolchain = findOrDefault(toolchains, [matcher](const ToolChain *tc) {
        return tc->language() == ProjectExplorer::Constants::CXX_LANGUAGE_ID && matcher(tc);
    });
    ToolChain * const cToolchain = findOrDefault(toolchains, [matcher](const ToolChain *tc) {
        return tc->language() == ProjectExplorer::Constants::C_LANGUAGE_ID && matcher(tc);
    });
    QList<ToolChain *> chosenToolchains;
    for (ToolChain * const tc : {cxxToolchain, cToolchain}) {
        if (tc)
            chosenToolchains << tc;
    };
    return chosenToolchains;
}

Kit *QmakeProjectImporter::createTemporaryKit(const QtProjectImporter::QtVersionData &data,
                                              const QString &parsedSpec,
                                              const QMakeStepConfig::TargetArchConfig &archConfig,
                                              const QMakeStepConfig::OsType &osType) const
{
    Q_UNUSED(osType) // TODO use this to select the right toolchain?
    return QtProjectImporter::createTemporaryKit(data,
                                                 [&data, parsedSpec, archConfig](Kit *k) -> void {
        for (ToolChain * const tc : preferredToolChains(data.qt, parsedSpec, archConfig))
            ToolChainKitAspect::setToolChain(k, tc);
        if (parsedSpec != data.qt->mkspec())
            QmakeKitAspect::setMkspec(k, parsedSpec, QmakeKitAspect::MkspecSource::Code);
    });
}

} // namespace Internal
} // namespace QmakeProjectManager
