/****************************************************************************
**
** Copyright (C) 2017 The Qt Company Ltd.
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

#pragma once

#include "cpptools_utils.h"
#include "projectinfo.h"

#include <QFutureInterface>

namespace CppTools {
namespace Internal {

class ProjectInfoGenerator
{
public:
    ProjectInfoGenerator(const QFutureInterface<ProjectInfo::ConstPtr> &futureInterface,
                         const ProjectExplorer::ProjectUpdateInfo &projectUpdateInfo);

    ProjectInfo::ConstPtr generate();

private:
    const QVector<ProjectPart::ConstPtr> createProjectParts(
            const ProjectExplorer::RawProjectPart &rawProjectPart,
            const Utils::FilePath &projectFilePath);
    ProjectPart::ConstPtr createProjectPart(const Utils::FilePath &projectFilePath,
                                       const ProjectExplorer::RawProjectPart &rawProjectPart,
                                       const ProjectFiles &projectFiles,
                                       const QString &partName,
                                       Language language,
                                       Utils::LanguageExtensions languageExtensions);

private:
    const QFutureInterface<ProjectInfo::ConstPtr> m_futureInterface;
    const ProjectExplorer::ProjectUpdateInfo &m_projectUpdateInfo;
    bool m_cToolchainMissing = false;
    bool m_cxxToolchainMissing = false;
};
} // namespace Internal
} // namespace CppTools
