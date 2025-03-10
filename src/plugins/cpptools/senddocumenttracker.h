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

#pragma once

#include "cpptools_global.h"

#include <QObject>

#include <limits>

namespace CppTools {

class CPPTOOLS_EXPORT SendDocumentTracker
{
public:
    void setLastSentRevision(int lastSentRevision);
    int lastSentRevision() const;

    void setLastCompletionPosition(int lastCompletionPosition);
    int lastCompletionPosition() const;

    void applyContentChange(int startPosition);

    bool shouldSendCompletion(int newCompletionPosition) const;
    bool shouldSendRevision(uint newRevision) const;
    bool shouldSendRevisionWithCompletionPosition(int newRevision, int newCompletionPosition) const;

private:
    bool changedBeforeCompletionPosition(int newCompletionPosition) const;

private:
    int m_lastSentRevision = -1;
    int m_lastCompletionPosition = -1;
    int m_contentChangeStartPosition = std::numeric_limits<int>::max();
};

#ifdef WITH_TESTS
namespace Internal {
class DocumentTrackerTest : public QObject
{
    Q_OBJECT

private slots:
    void testDefaultLastSentRevision();
    void testSetRevision();
    void testSetLastCompletionPosition();
    void testApplyContentChange();
    void testDontSendCompletionIfPositionIsEqual();
    void testSendCompletionIfPositionIsDifferent();
    void testSendCompletionIfChangeIsBeforeCompletionPositionAndPositionIsEqual();
    void testDontSendCompletionIfChangeIsAfterCompletionPositionAndPositionIsEqual();
    void testDontSendRevisionIfRevisionIsEqual();
    void testSendRevisionIfRevisionIsDifferent();
    void testDontSendRevisionWithDefaults();
    void testDontSendIfRevisionIsDifferentAndCompletionPositionIsEqualAndNoContentChange();
    void testDontSendIfRevisionIsDifferentAndCompletionPositionIsDifferentAndNoContentChange();
    void testDontSendIfRevisionIsEqualAndCompletionPositionIsDifferentAndNoContentChange();
    void testSendIfChangeIsBeforeCompletionAndPositionIsEqualAndRevisionIsDifferent();
    void testDontSendIfChangeIsAfterCompletionPositionAndRevisionIsDifferent();
    void testSendIfChangeIsBeforeCompletionPositionAndRevisionIsDifferent();
    void testResetChangedContentStartPositionIfLastRevisionIsSet();
};
} // namespace Internal
#endif // WITH_TESTS

} // namespace CppTools
