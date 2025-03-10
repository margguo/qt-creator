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

#include <QtGlobal>

#include "objectnodeinstance.h"

#include <QQuickItem>
#include <designersupportdelegate.h>

namespace QmlDesigner {
namespace Internal {

class QuickItemNodeInstance : public ObjectNodeInstance
{
public:
    using Pointer = QSharedPointer<QuickItemNodeInstance>;
    using WeakPointer = QWeakPointer<QuickItemNodeInstance>;

    ~QuickItemNodeInstance() override;
    void handleObjectDeletion(QObject *object) override;

    static Pointer create(QObject *objectToBeWrapped);
    static void createEffectItem(bool createEffectItem);
    static void enableUnifiedRenderPath(bool createEffectItem);

    void initialize(const ObjectNodeInstance::Pointer &objectNodeInstance,
                    InstanceContainer::NodeFlags flags) override;

    QQuickItem *contentItem() const override;
    bool hasContent() const override;

    QRectF contentItemBoundingBox() const override;
    QRectF boundingRect() const override;
    QTransform contentTransform() const override;
    QTransform sceneTransform() const override;
    double opacity() const override;
    double rotation() const override;
    double scale() const override;
    QPointF transformOriginPoint() const override;
    double zValue() const override;
    QPointF position() const override;
    QSizeF size() const override;
    QTransform transform() const override;
    QTransform contentItemTransform() const override;
    int penWidth() const override;

    QImage renderImage() const override;
    QImage renderPreviewImage(const QSize &previewImageSize) const override;

    QSharedPointer<QQuickItemGrabResult> createGrabResult() const override;

    void updateAllDirtyNodesRecursive() override;


    QObject *parent() const override;
    QList<ServerNodeInstance> childItems() const override;

    void reparent(const ObjectNodeInstance::Pointer &oldParentInstance, const PropertyName &oldParentProperty, const ObjectNodeInstance::Pointer &newParentInstance, const PropertyName &newParentProperty) override;

    void setPropertyVariant(const PropertyName &name, const QVariant &value) override;
    void setPropertyBinding(const PropertyName &name, const QString &expression) override;
    QVariant property(const PropertyName &name) const override;
    void resetProperty(const PropertyName &name) override;

    bool isAnchoredByChildren() const override;
    bool hasAnchor(const PropertyName &name) const override;
    QPair<PropertyName, ServerNodeInstance> anchor(const PropertyName &name) const override;
    bool isAnchoredBySibling() const override;
    bool isResizable() const override;
    bool isMovable() const override;
    bool isQuickItem() const override;

    QList<ServerNodeInstance> stateInstances() const override;

    void doComponentComplete() override;

    QList<QQuickItem*> allItemsRecursive() const override;
    QStringList allStates() const override;

    static void updateDirtyNode(QQuickItem *item);
    static bool unifiedRenderPath();
    static bool unifiedRenderPathOrQt6();

protected:
    explicit QuickItemNodeInstance(QQuickItem*);
    QQuickItem *quickItem() const;
    void setMovable(bool movable);
    void setResizable(bool resizable);
    void setHasContent(bool hasContent);
    DesignerSupport *designerSupport() const;
    Qt5NodeInstanceServer *qt5NodeInstanceServer() const;
    void updateDirtyNodesRecursive(QQuickItem *parentItem) const;
    void updateAllDirtyNodesRecursive(QQuickItem *parentItem) const;
    QRectF boundingRectWithStepChilds(QQuickItem *parentItem) const;
    void resetHorizontal();
    void resetVertical();
    QList<ServerNodeInstance> childItemsForChild(QQuickItem *item) const;
    void refresh();
    static bool anyItemHasContent(QQuickItem *quickItem);
    static bool childItemsHaveContent(QQuickItem *quickItem);

    double x() const;
    double y() const;
    bool checkIfRefFromEffect(qint32 id);

private: //variables
    QPointer<QQuickItem> m_contentItem;
    bool m_isResizable;
    bool m_isMovable;
    bool m_hasHeight;
    bool m_hasWidth;
    bool m_hasContent;
    double m_x;
    double m_y;
    double m_width;
    double m_height;
    static bool s_createEffectItem;
    static bool s_unifiedRenderPath;
};

} // namespace Internal
} // namespace QmlDesigner
