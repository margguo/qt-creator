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

#include "devicemanager.h"

#include "idevicefactory.h"

#include <coreplugin/icore.h>
#include <coreplugin/messagemanager.h>

#include <projectexplorer/projectexplorerconstants.h>
#include <utils/algorithm.h>
#include <utils/fileutils.h>
#include <utils/persistentsettings.h>
#include <utils/portlist.h>
#include <utils/qtcassert.h>
#include <utils/qtcprocess.h>
#include <utils/stringutils.h>

#include <QDateTime>
#include <QFileInfo>
#include <QHash>
#include <QList>
#include <QString>
#include <QVariantList>

#include <limits>
#include <memory>

using namespace Utils;

namespace ProjectExplorer {
namespace Internal {

const char DeviceManagerKey[] = "DeviceManager";
const char DeviceListKey[] = "DeviceList";
const char DefaultDevicesKey[] = "DefaultDevices";

class DeviceManagerPrivate
{
public:
    DeviceManagerPrivate() = default;

    int indexForId(Utils::Id id) const
    {
        for (int i = 0; i < devices.count(); ++i) {
            if (devices.at(i)->id() == id)
                return i;
        }
        return -1;
    }

    static DeviceManager *clonedInstance;
    QList<IDevice::Ptr> devices;
    QHash<Utils::Id, Utils::Id> defaultDevices;

    Utils::PersistentSettingsWriter *writer = nullptr;
};
DeviceManager *DeviceManagerPrivate::clonedInstance = nullptr;

} // namespace Internal

using namespace Internal;

DeviceManager *DeviceManager::m_instance = nullptr;

DeviceManager *DeviceManager::instance()
{
    return m_instance;
}

int DeviceManager::deviceCount() const
{
    return d->devices.count();
}

void DeviceManager::replaceInstance()
{
    const QList<Id> newIds =
        Utils::transform(DeviceManagerPrivate::clonedInstance->d->devices, &IDevice::id);

    for (IDevice::Ptr dev : m_instance->d->devices) {
        if (!newIds.contains(dev->id()))
            dev->aboutToBeRemoved();
    }

    copy(DeviceManagerPrivate::clonedInstance, instance(), false);

    emit instance()->deviceListReplaced();
    emit instance()->updated();
}

void DeviceManager::removeClonedInstance()
{
    delete DeviceManagerPrivate::clonedInstance;
    DeviceManagerPrivate::clonedInstance = nullptr;
}

DeviceManager *DeviceManager::cloneInstance()
{
    QTC_ASSERT(!DeviceManagerPrivate::clonedInstance, return nullptr);

    DeviceManagerPrivate::clonedInstance = new DeviceManager(false);
    copy(instance(), DeviceManagerPrivate::clonedInstance, true);
    return DeviceManagerPrivate::clonedInstance;
}

void DeviceManager::copy(const DeviceManager *source, DeviceManager *target, bool deep)
{
    if (deep) {
        foreach (const IDevice::ConstPtr &device, source->d->devices)
            target->d->devices << device->clone();
    } else {
        target->d->devices = source->d->devices;
    }
    target->d->defaultDevices = source->d->defaultDevices;
}

void DeviceManager::save()
{
    if (d->clonedInstance == this || !d->writer)
        return;
    QVariantMap data;
    data.insert(QLatin1String(DeviceManagerKey), toMap());
    d->writer->save(data, Core::ICore::dialogParent());
}

void DeviceManager::load()
{
    QTC_ASSERT(!d->writer, return);

    // Only create writer now: We do not want to save before the settings were read!
    d->writer = new PersistentSettingsWriter(settingsFilePath("devices.xml"), "QtCreatorDevices");

    Utils::PersistentSettingsReader reader;
    // read devices file from global settings path
    QHash<Utils::Id, Utils::Id> defaultDevices;
    QList<IDevice::Ptr> sdkDevices;
    if (reader.load(systemSettingsFilePath("devices.xml")))
        sdkDevices = fromMap(reader.restoreValues().value(DeviceManagerKey).toMap(), &defaultDevices);
    // read devices file from user settings path
    QList<IDevice::Ptr> userDevices;
    if (reader.load(settingsFilePath("devices.xml")))
        userDevices = fromMap(reader.restoreValues().value(DeviceManagerKey).toMap(), &defaultDevices);
    // Insert devices into the model. Prefer the higher device version when there are multiple
    // devices with the same id.
    foreach (IDevice::Ptr device, userDevices) {
        foreach (const IDevice::Ptr &sdkDevice, sdkDevices) {
            if (device->id() == sdkDevice->id()) {
                if (device->version() < sdkDevice->version())
                    device = sdkDevice;
                sdkDevices.removeOne(sdkDevice);
                break;
            }
        }
        addDevice(device);
    }
    // Append the new SDK devices to the model.
    foreach (const IDevice::Ptr &sdkDevice, sdkDevices)
        addDevice(sdkDevice);

    // Overwrite with the saved default devices.
    for (auto itr = defaultDevices.constBegin(); itr != defaultDevices.constEnd(); ++itr) {
        IDevice::ConstPtr device = find(itr.value());
        if (device)
            d->defaultDevices[device->type()] = device->id();
    }

    emit devicesLoaded();
}

QList<IDevice::Ptr> DeviceManager::fromMap(const QVariantMap &map,
                                           QHash<Utils::Id, Utils::Id> *defaultDevices)
{
    QList<IDevice::Ptr> devices;

    if (defaultDevices) {
        const QVariantMap defaultDevsMap = map.value(DefaultDevicesKey).toMap();
        for (auto it = defaultDevsMap.constBegin(); it != defaultDevsMap.constEnd(); ++it)
            defaultDevices->insert(Utils::Id::fromString(it.key()), Utils::Id::fromSetting(it.value()));
    }
    const QVariantList deviceList = map.value(QLatin1String(DeviceListKey)).toList();
    foreach (const QVariant &v, deviceList) {
        const QVariantMap map = v.toMap();
        const IDeviceFactory * const factory = restoreFactory(map);
        if (!factory)
            continue;
        const IDevice::Ptr device = factory->construct();
        QTC_ASSERT(device, continue);
        device->fromMap(map);
        devices << device;
    }
    return devices;
}

QVariantMap DeviceManager::toMap() const
{
    QVariantMap map;
    QVariantMap defaultDeviceMap;
    using TypeIdHash = QHash<Utils::Id, Utils::Id>;
    for (TypeIdHash::ConstIterator it = d->defaultDevices.constBegin();
             it != d->defaultDevices.constEnd(); ++it) {
        defaultDeviceMap.insert(it.key().toString(), it.value().toSetting());
    }
    map.insert(QLatin1String(DefaultDevicesKey), defaultDeviceMap);
    QVariantList deviceList;
    foreach (const IDevice::ConstPtr &device, d->devices)
        deviceList << device->toMap();
    map.insert(QLatin1String(DeviceListKey), deviceList);
    return map;
}

FilePath DeviceManager::settingsFilePath(const QString &extension)
{
    return Core::ICore::userResourcePath(extension);
}

FilePath DeviceManager::systemSettingsFilePath(const QString &deviceFileRelativePath)
{
    return Core::ICore::installerResourcePath(deviceFileRelativePath);
}

void DeviceManager::addDevice(const IDevice::ConstPtr &_device)
{
    const IDevice::Ptr device = _device->clone();

    QStringList names;
    foreach (const IDevice::ConstPtr &tmp, d->devices) {
        if (tmp->id() != device->id())
            names << tmp->displayName();
    }

    device->setDisplayName(Utils::makeUniquelyNumbered(device->displayName(), names));

    const int pos = d->indexForId(device->id());

    if (!defaultDevice(device->type()))
        d->defaultDevices.insert(device->type(), device->id());
    if (this == DeviceManager::instance() && d->clonedInstance)
        d->clonedInstance->addDevice(device->clone());

    if (pos >= 0) {
        d->devices[pos] = device;
        emit deviceUpdated(device->id());
    } else {
        d->devices << device;
        emit deviceAdded(device->id());
    }

    emit updated();
}

void DeviceManager::removeDevice(Utils::Id id)
{
    const IDevice::Ptr device = mutableDevice(id);
    QTC_ASSERT(device, return);
    QTC_ASSERT(this != instance() || device->isAutoDetected(), return);

    const bool wasDefault = d->defaultDevices.value(device->type()) == device->id();
    const Utils::Id deviceType = device->type();
    d->devices.removeAt(d->indexForId(id));
    emit deviceRemoved(device->id());

    if (wasDefault) {
        for (int i = 0; i < d->devices.count(); ++i) {
            if (deviceAt(i)->type() == deviceType) {
                d->defaultDevices.insert(deviceAt(i)->type(), deviceAt(i)->id());
                emit deviceUpdated(deviceAt(i)->id());
                break;
            }
        }
    }
    if (this == instance() && d->clonedInstance)
        d->clonedInstance->removeDevice(id);

    emit updated();
}

void DeviceManager::setDeviceState(Utils::Id deviceId, IDevice::DeviceState deviceState)
{
    // To see the state change in the DeviceSettingsWidget. This has to happen before
    // the pos check below, in case the device is only present in the cloned instance.
    if (this == instance() && d->clonedInstance)
        d->clonedInstance->setDeviceState(deviceId, deviceState);

    const int pos = d->indexForId(deviceId);
    if (pos < 0)
        return;
    IDevice::Ptr &device = d->devices[pos];
    if (device->deviceState() == deviceState)
        return;

    device->setDeviceState(deviceState);
    emit deviceUpdated(deviceId);
    emit updated();
}

bool DeviceManager::isLoaded() const
{
    return d->writer;
}

IDevice::ConstPtr DeviceManager::deviceForPath(const FilePath &path)
{
    for (IDevice::Ptr &dev : instance()->d->devices) {
        if (dev->handlesFile(path))
            return dev;
    }
    return {};
}

IDevice::ConstPtr DeviceManager::defaultDesktopDevice()
{
    return m_instance->defaultDevice(Constants::DESKTOP_DEVICE_TYPE);
}

void DeviceManager::setDefaultDevice(Utils::Id id)
{
    QTC_ASSERT(this != instance(), return);

    const IDevice::ConstPtr &device = find(id);
    QTC_ASSERT(device, return);
    const IDevice::ConstPtr &oldDefaultDevice = defaultDevice(device->type());
    if (device == oldDefaultDevice)
        return;
    d->defaultDevices.insert(device->type(), device->id());
    emit deviceUpdated(device->id());
    emit deviceUpdated(oldDefaultDevice->id());

    emit updated();
}

const IDeviceFactory *DeviceManager::restoreFactory(const QVariantMap &map)
{
    const Utils::Id deviceType = IDevice::typeFromMap(map);
    IDeviceFactory *factory = Utils::findOrDefault(IDeviceFactory::allDeviceFactories(),
        [&map, deviceType](IDeviceFactory *factory) {
            return factory->canRestore(map) && factory->deviceType() == deviceType;
        });

    if (!factory)
        qWarning("Warning: No factory found for device '%s' of type '%s'.",
                 qPrintable(IDevice::idFromMap(map).toString()),
                 qPrintable(IDevice::typeFromMap(map).toString()));
    return factory;
}

DeviceManager::DeviceManager(bool isInstance) : d(std::make_unique<DeviceManagerPrivate>())
{
    QTC_ASSERT(isInstance == !m_instance, return);

    if (!isInstance)
        return;

    m_instance = this;
    connect(Core::ICore::instance(), &Core::ICore::saveSettingsRequested,
            this, &DeviceManager::save);

    DeviceFileHooks deviceHooks;

    deviceHooks.isExecutableFile = [](const FilePath &filePath) {
        auto device = DeviceManager::deviceForPath(filePath);
        QTC_ASSERT(device, return false);
        return device->isExecutableFile(filePath);
    };

    deviceHooks.isReadableFile = [](const FilePath &filePath) {
        auto device = DeviceManager::deviceForPath(filePath);
        QTC_ASSERT(device, return false);
        return device->isReadableFile(filePath);
    };

    deviceHooks.isReadableDir = [](const FilePath &filePath) {
        auto device = DeviceManager::deviceForPath(filePath);
        QTC_ASSERT(device, return false);
        return device->isReadableDirectory(filePath);
    };

    deviceHooks.isWritableDir = [](const FilePath &filePath) {
        auto device = DeviceManager::deviceForPath(filePath);
        QTC_ASSERT(device, return false);
        return device->isWritableDirectory(filePath);
    };

    deviceHooks.isWritableFile = [](const FilePath &filePath) {
        auto device = DeviceManager::deviceForPath(filePath);
        QTC_ASSERT(device, return false);
        return device->isWritableFile(filePath);
    };

    deviceHooks.isFile = [](const FilePath &filePath) {
        auto device = DeviceManager::deviceForPath(filePath);
        QTC_ASSERT(device, return false);
        return device->isFile(filePath);
    };

    deviceHooks.isDir = [](const FilePath &filePath) {
        auto device = DeviceManager::deviceForPath(filePath);
        QTC_ASSERT(device, return false);
        return device->isDirectory(filePath);
    };

    deviceHooks.ensureWritableDir = [](const FilePath &filePath) {
        auto device = DeviceManager::deviceForPath(filePath);
        QTC_ASSERT(device, return false);
        return device->ensureWritableDirectory(filePath);
    };

    deviceHooks.ensureExistingFile = [](const FilePath &filePath) {
        auto device = DeviceManager::deviceForPath(filePath);
        QTC_ASSERT(device, return false);
        return device->ensureExistingFile(filePath);
    };

    deviceHooks.createDir = [](const FilePath &filePath) {
        auto device = DeviceManager::deviceForPath(filePath);
        QTC_ASSERT(device, return false);
        return device->createDirectory(filePath);
    };

    deviceHooks.exists = [](const FilePath &filePath) {
        auto device = DeviceManager::deviceForPath(filePath);
        QTC_ASSERT(device, return false);
        return device->exists(filePath);
    };

    deviceHooks.removeFile = [](const FilePath &filePath) {
        auto device = DeviceManager::deviceForPath(filePath);
        QTC_ASSERT(device, return false);
        return device->removeFile(filePath);
    };

    deviceHooks.removeRecursively = [](const FilePath &filePath) {
        auto device = DeviceManager::deviceForPath(filePath);
        QTC_ASSERT(device, return false);
        return device->removeRecursively(filePath);
    };

    deviceHooks.copyFile = [](const FilePath &filePath, const FilePath &target) {
        auto device = DeviceManager::deviceForPath(filePath);
        QTC_ASSERT(device, return false);
        return device->copyFile(filePath, target);
    };

    deviceHooks.renameFile = [](const FilePath &filePath, const FilePath &target) {
        auto device = DeviceManager::deviceForPath(filePath);
        QTC_ASSERT(device, return false);
        return device->renameFile(filePath, target);
    };

    deviceHooks.searchInPath = [](const FilePath &filePath, const FilePaths &dirs) {
        auto device = DeviceManager::deviceForPath(filePath);
        QTC_ASSERT(device, return FilePath{});
        return device->searchExecutable(filePath.path(), dirs);
    };

    deviceHooks.symLinkTarget = [](const FilePath &filePath) {
        auto device = DeviceManager::deviceForPath(filePath);
        QTC_ASSERT(device, return FilePath{});
        return device->symLinkTarget(filePath);
    };

    deviceHooks.dirEntries = [](const FilePath &filePath, const QStringList &nameFilters,
                                QDir::Filters filters, QDir::SortFlags sort) {
        auto device = DeviceManager::deviceForPath(filePath);
        QTC_ASSERT(device, return FilePaths());
        return device->directoryEntries(filePath, nameFilters, filters, sort);
    };

    deviceHooks.fileContents = [](const FilePath &filePath, qint64 maxSize, qint64 offset) {
        auto device = DeviceManager::deviceForPath(filePath);
        QTC_ASSERT(device, return QByteArray());
        return device->fileContents(filePath, maxSize, offset);
    };

    deviceHooks.writeFileContents = [](const FilePath &filePath, const QByteArray &data) {
        auto device = DeviceManager::deviceForPath(filePath);
        QTC_ASSERT(device, return false);
        return device->writeFileContents(filePath, data);
    };

    deviceHooks.lastModified = [](const FilePath &filePath) {
        auto device = DeviceManager::deviceForPath(filePath);
        QTC_ASSERT(device, return QDateTime());
        return device->lastModified(filePath);
    };

    deviceHooks.permissions = [](const FilePath &filePath) {
        auto device = DeviceManager::deviceForPath(filePath);
        QTC_ASSERT(device, return QFile::Permissions());
        return device->permissions(filePath);
    };

    deviceHooks.setPermissions = [](const FilePath &filePath, QFile::Permissions permissions) {
        auto device = DeviceManager::deviceForPath(filePath);
        QTC_ASSERT(device, return false);
        return device->setPermissions(filePath, permissions);
    };

    deviceHooks.osType = [](const FilePath &filePath) {
        auto device = DeviceManager::deviceForPath(filePath);
        QTC_ASSERT(device, return OsTypeOther);
        return device->osType();
    };

    deviceHooks.environment = [](const FilePath &filePath) {
        auto device = DeviceManager::deviceForPath(filePath);
        QTC_ASSERT(device, return Environment{});
        return device->systemEnvironment();
    };

    deviceHooks.fileSize = [](const FilePath &filePath) {
        auto device = DeviceManager::deviceForPath(filePath);
        QTC_ASSERT(device, return qint64(-1));
        return device->fileSize(filePath);
    };

    FileUtils::setDeviceFileHooks(deviceHooks);

    DeviceProcessHooks processHooks;

    processHooks.startProcessHook = [](QtcProcess &process) {
        FilePath filePath = process.commandLine().executable();
        auto device = DeviceManager::deviceForPath(filePath);
        QTC_ASSERT(device, return);
        device->runProcess(process);
    };

    processHooks.systemEnvironmentForBinary = [](const FilePath &filePath) {
        auto device = DeviceManager::deviceForPath(filePath);
        QTC_ASSERT(device, return Environment());
        return device->systemEnvironment();
    };

    QtcProcess::setRemoteProcessHooks(processHooks);
}

DeviceManager::~DeviceManager()
{
    if (d->clonedInstance != this)
        delete d->writer;
    if (m_instance == this)
        m_instance = nullptr;
}

IDevice::ConstPtr DeviceManager::deviceAt(int idx) const
{
    QTC_ASSERT(idx >= 0 && idx < deviceCount(), return IDevice::ConstPtr());
    return d->devices.at(idx);
}

IDevice::Ptr DeviceManager::mutableDevice(Utils::Id id) const
{
    const int index = d->indexForId(id);
    return index == -1 ? IDevice::Ptr() : d->devices.at(index);
}

bool DeviceManager::hasDevice(const QString &name) const
{
    return Utils::anyOf(d->devices, [&name](const IDevice::Ptr &device) {
        return device->displayName() == name;
    });
}

IDevice::ConstPtr DeviceManager::find(Utils::Id id) const
{
    const int index = d->indexForId(id);
    return index == -1 ? IDevice::ConstPtr() : deviceAt(index);
}

IDevice::ConstPtr DeviceManager::defaultDevice(Utils::Id deviceType) const
{
    const Utils::Id id = d->defaultDevices.value(deviceType);
    return id.isValid() ? find(id) : IDevice::ConstPtr();
}

} // namespace ProjectExplorer


#ifdef WITH_TESTS
#include <projectexplorer/projectexplorer.h>
#include <QSignalSpy>
#include <QTest>
#include <QUuid>

namespace ProjectExplorer {

class TestDevice : public IDevice
{
public:
    TestDevice()
    {
        setupId(AutoDetected, Utils::Id::fromString(QUuid::createUuid().toString()));
        setType(testTypeId());
        setMachineType(Hardware);
        setOsType(HostOsInfo::hostOs());
        setDisplayType("blubb");
    }

    static Utils::Id testTypeId() { return "TestType"; }
private:
    IDeviceWidget *createWidget() override { return nullptr; }
    DeviceProcessSignalOperation::Ptr signalOperation() const override
    {
        return DeviceProcessSignalOperation::Ptr();
    }
};

class TestDeviceFactory final : public IDeviceFactory
{
public:
    TestDeviceFactory() : IDeviceFactory(TestDevice::testTypeId())
    {
        setConstructionFunction([] { return IDevice::Ptr(new TestDevice); });
    }
};

void ProjectExplorerPlugin::testDeviceManager()
{
    TestDeviceFactory factory;

    TestDevice::Ptr dev = IDevice::Ptr(new TestDevice);
    dev->setDisplayName(QLatin1String("blubbdiblubbfurz!"));
    QVERIFY(dev->isAutoDetected());
    QCOMPARE(dev->deviceState(), IDevice::DeviceStateUnknown);
    QCOMPARE(dev->type(), TestDevice::testTypeId());

    TestDevice::Ptr dev2 = dev->clone();
    QCOMPARE(dev->id(), dev2->id());

    DeviceManager * const mgr = DeviceManager::instance();
    QVERIFY(!mgr->find(dev->id()));
    const int oldDeviceCount = mgr->deviceCount();

    QSignalSpy deviceAddedSpy(mgr, &DeviceManager::deviceAdded);
    QSignalSpy deviceRemovedSpy(mgr, &DeviceManager::deviceRemoved);
    QSignalSpy deviceUpdatedSpy(mgr, &DeviceManager::deviceUpdated);
    QSignalSpy deviceListReplacedSpy(mgr, &DeviceManager::deviceListReplaced);
    QSignalSpy updatedSpy(mgr, &DeviceManager::updated);

    mgr->addDevice(dev);
    QCOMPARE(mgr->deviceCount(), oldDeviceCount + 1);
    QVERIFY(mgr->find(dev->id()));
    QVERIFY(mgr->hasDevice(dev->displayName()));
    QCOMPARE(deviceAddedSpy.count(), 1);
    QCOMPARE(deviceRemovedSpy.count(), 0);
    QCOMPARE(deviceUpdatedSpy.count(), 0);
    QCOMPARE(deviceListReplacedSpy.count(), 0);
    QCOMPARE(updatedSpy.count(), 1);
    deviceAddedSpy.clear();
    updatedSpy.clear();

    mgr->setDeviceState(dev->id(), IDevice::DeviceStateUnknown);
    QCOMPARE(deviceAddedSpy.count(), 0);
    QCOMPARE(deviceRemovedSpy.count(), 0);
    QCOMPARE(deviceUpdatedSpy.count(), 0);
    QCOMPARE(deviceListReplacedSpy.count(), 0);
    QCOMPARE(updatedSpy.count(), 0);

    mgr->setDeviceState(dev->id(), IDevice::DeviceReadyToUse);
    QCOMPARE(mgr->find(dev->id())->deviceState(), IDevice::DeviceReadyToUse);
    QCOMPARE(deviceAddedSpy.count(), 0);
    QCOMPARE(deviceRemovedSpy.count(), 0);
    QCOMPARE(deviceUpdatedSpy.count(), 1);
    QCOMPARE(deviceListReplacedSpy.count(), 0);
    QCOMPARE(updatedSpy.count(), 1);
    deviceUpdatedSpy.clear();
    updatedSpy.clear();

    mgr->addDevice(dev2);
    QCOMPARE(mgr->deviceCount(), oldDeviceCount + 1);
    QVERIFY(mgr->find(dev->id()));
    QCOMPARE(deviceAddedSpy.count(), 0);
    QCOMPARE(deviceRemovedSpy.count(), 0);
    QCOMPARE(deviceUpdatedSpy.count(), 1);
    QCOMPARE(deviceListReplacedSpy.count(), 0);
    QCOMPARE(updatedSpy.count(), 1);
    deviceUpdatedSpy.clear();
    updatedSpy.clear();

    TestDevice::Ptr dev3 = IDevice::Ptr(new TestDevice);
    QVERIFY(dev->id() != dev3->id());

    dev3->setDisplayName(dev->displayName());
    mgr->addDevice(dev3);
    QCOMPARE(mgr->deviceAt(mgr->deviceCount() - 1)->displayName(),
             QString(dev3->displayName() + QLatin1Char('2')));
    QCOMPARE(deviceAddedSpy.count(), 1);
    QCOMPARE(deviceRemovedSpy.count(), 0);
    QCOMPARE(deviceUpdatedSpy.count(), 0);
    QCOMPARE(deviceListReplacedSpy.count(), 0);
    QCOMPARE(updatedSpy.count(), 1);
    deviceAddedSpy.clear();
    updatedSpy.clear();

    mgr->removeDevice(dev->id());
    mgr->removeDevice(dev3->id());
    QCOMPARE(mgr->deviceCount(), oldDeviceCount);
    QVERIFY(!mgr->find(dev->id()));
    QVERIFY(!mgr->find(dev3->id()));
    QCOMPARE(deviceAddedSpy.count(), 0);
    QCOMPARE(deviceRemovedSpy.count(), 2);
//    QCOMPARE(deviceUpdatedSpy.count(), 0); Uncomment once the "default" stuff is gone.
    QCOMPARE(deviceListReplacedSpy.count(), 0);
    QCOMPARE(updatedSpy.count(), 2);
}

} // namespace ProjectExplorer

#endif // WITH_TESTS
