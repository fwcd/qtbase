// Copyright (C) 2014 Ivan Komissarov <ABBAPOH@gmail.com>
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include <QTest>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QTemporaryFile>

#include <stdarg.h>

#include "../../../../manual/qstorageinfo/printvolumes.cpp"

#ifdef Q_OS_LINUX
#  include "../../../../../src/corelib/io/qstorageinfo_linux_p.h"
#endif

class tst_QStorageInfo : public QObject
{
    Q_OBJECT
private slots:
    void defaultValues();
    void dump();
    void operatorEqual();
    void operatorNotEqual();
    void root();
    void currentStorage();
    void storageList();
    void tempFile();
    void caching();

#if defined(Q_OS_LINUX) && defined(QT_BUILD_INTERNAL)
    void testParseMountInfo_data();
    void testParseMountInfo();
    void testParseMountInfo_filtered_data();
    void testParseMountInfo_filtered();
#endif
};

void tst_QStorageInfo::defaultValues()
{
    QStorageInfo storage;

    QVERIFY(!storage.isValid());
    QVERIFY(!storage.isReady());
    QVERIFY(storage.rootPath().isEmpty());
    QVERIFY(!storage.isRoot());
    QVERIFY(storage.device().isEmpty());
    QVERIFY(storage.fileSystemType().isEmpty());
    QCOMPARE(storage.bytesTotal(), -1);
    QCOMPARE(storage.bytesFree(), -1);
    QCOMPARE(storage.bytesAvailable(), -1);
}

static int qInfoPrinter(const char *format, ...)
{
    static char buf[1024];
    static size_t bufuse = 0;

    va_list ap;
    va_start(ap, format); // use variable arg list
    int n = qvsnprintf(buf + bufuse, sizeof(buf) - bufuse, format, ap);
    va_end(ap);

    bufuse += n;
    if (bufuse >= sizeof(buf) - 1 || format[strlen(format) - 1] == '\n') {
        // flush
        QtMessageHandler qt_message_print = qInstallMessageHandler(0);
        qInstallMessageHandler(qt_message_print);   // restore the handler
        qt_message_print(QtInfoMsg, QMessageLogContext(), QString::fromLocal8Bit(buf).trimmed());
        bufuse = 0;
    }

    return 1;
}

void tst_QStorageInfo::dump()
{
    printVolumes(QStorageInfo::mountedVolumes(), qInfoPrinter);
}

void tst_QStorageInfo::operatorEqual()
{
    {
        QStorageInfo storage1 = QStorageInfo::root();
        QStorageInfo storage2(QDir::rootPath());
        QCOMPARE(storage1, storage2);
    }

    {
        QStorageInfo storage1(QCoreApplication::applicationDirPath());
        QStorageInfo storage2(QCoreApplication::applicationFilePath());
        QCOMPARE(storage1, storage2);
    }

    {
        QStorageInfo storage1;
        QStorageInfo storage2;
        QCOMPARE(storage1, storage2);
    }

    // Test copy ctor
    {
        QStorageInfo storage1 = QStorageInfo::root();
        QStorageInfo storage2(storage1);
        QCOMPARE(storage1, storage2);
    }
}

void tst_QStorageInfo::operatorNotEqual()
{
    QStorageInfo storage1 = QStorageInfo::root();
    QStorageInfo storage2;
    QCOMPARE_NE(storage1, storage2);
}

void tst_QStorageInfo::root()
{
    QStorageInfo storage = QStorageInfo::root();

    QVERIFY(storage.isValid());
    QVERIFY(storage.isReady());
    QCOMPARE(storage.rootPath(), QDir::rootPath());
    QVERIFY(storage.isRoot());
    QVERIFY(!storage.device().isEmpty());
    QVERIFY(!storage.fileSystemType().isEmpty());
#ifndef Q_OS_HAIKU
    QCOMPARE_GE(storage.bytesTotal(), 0);
    QCOMPARE_GE(storage.bytesFree(), 0);
    QCOMPARE_GE(storage.bytesAvailable(), 0);
#endif
}

void tst_QStorageInfo::currentStorage()
{
    QString appPath = QCoreApplication::applicationFilePath();
    QStorageInfo storage(appPath);
    QVERIFY(storage.isValid());
    QVERIFY(storage.isReady());
    QVERIFY(appPath.startsWith(storage.rootPath(), Qt::CaseInsensitive));
    QVERIFY(!storage.device().isEmpty());
    QVERIFY(!storage.fileSystemType().isEmpty());
    QCOMPARE_GE(storage.bytesTotal(), 0);
    QCOMPARE_GE(storage.bytesFree(), 0);
    QCOMPARE_GE(storage.bytesAvailable(), 0);
}

void tst_QStorageInfo::storageList()
{
    QStorageInfo root = QStorageInfo::root();

    QList<QStorageInfo> volumes = QStorageInfo::mountedVolumes();

    // at least, root storage should be present
    QVERIFY(volumes.contains(root));
    volumes.removeOne(root);
    QVERIFY(!volumes.contains(root));

    for (const QStorageInfo &storage : std::as_const(volumes)) {
        if (!storage.isReady())
            continue;

        QVERIFY(storage.isValid());
        QVERIFY(!storage.isRoot());
#ifndef Q_OS_WIN
        QVERIFY(!storage.device().isEmpty());
        QVERIFY(!storage.fileSystemType().isEmpty());
#endif
    }
}

static bool checkFilesystemGoodForWriting(QTemporaryFile &file, QStorageInfo &storage)
{
#ifdef Q_OS_LINUX
    auto reconstructAt = [](auto *where, auto &&... how) {
        // it's very difficult to convince QTemporaryFile to change the path...
        std::destroy_at(where);
        q20::construct_at(where, std::forward<decltype(how)>(how)...);
    };
    if (storage.fileSystemType() == "btrfs") {
        // let's see if we can find another, writable FS
        QString runtimeDir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
        if (!runtimeDir.isEmpty()) {
            reconstructAt(&file, runtimeDir + "/XXXXXX");
            if (file.open()) {
                storage.setPath(file.fileName());
                if (storage.fileSystemType() != "btrfs")
                    return true;
            }
        }
        QTest::qSkip("btrfs does not synchronously update free space; this test would fail",
                     __FILE__, __LINE__);
        return false;
    }
#elif defined(Q_OS_DARWIN)
    Q_UNUSED(file);
    if (storage.fileSystemType() == "apfs") {
        QTest::qSkip("APFS does not synchronously update free space; this test would fail",
                     __FILE__, __LINE__);
        return false;
    }
#else
    Q_UNUSED(file);
    Q_UNUSED(storage);
#endif
    return true;
}

void tst_QStorageInfo::tempFile()
{
    QTemporaryFile file;
    QVERIFY2(file.open(), qPrintable(file.errorString()));

    QStorageInfo storage1(file.fileName());
    if (!checkFilesystemGoodForWriting(file, storage1))
        return;

    qint64 free = storage1.bytesFree();
    QCOMPARE_NE(free, -1);

    file.write(QByteArray(1024*1024, '1'));
    file.flush();
    file.close();

    QStorageInfo storage2(file.fileName());
    QCOMPARE_NE(free, storage2.bytesFree());
}

void tst_QStorageInfo::caching()
{
    QTemporaryFile file;
    QVERIFY2(file.open(), qPrintable(file.errorString()));

    QStorageInfo storage1(file.fileName());
    if (!checkFilesystemGoodForWriting(file, storage1))
        return;

    qint64 free = storage1.bytesFree();
    QStorageInfo storage2(storage1);
    QCOMPARE(free, storage2.bytesFree());
    QCOMPARE_NE(free, -1);

    file.write(QByteArray(1024*1024, '\0'));
    file.flush();

    QCOMPARE(free, storage1.bytesFree());
    QCOMPARE(free, storage2.bytesFree());
    storage2.refresh();
    QCOMPARE(storage1, storage2);
    QCOMPARE_NE(free, storage2.bytesFree());
}

#if defined(Q_OS_LINUX) && defined(QT_BUILD_INTERNAL)
void tst_QStorageInfo::testParseMountInfo_data()
{
    QTest::addColumn<QByteArray>("line");
    QTest::addColumn<MountInfo>("expected");

    QTest::newRow("tmpfs")
        << "17 25 0:18 / /dev rw,nosuid,relatime shared:2 - tmpfs tmpfs rw,seclabel,mode=755\n"_ba
        << MountInfo{"/dev", "tmpfs", "tmpfs", "", makedev(0, 18)};
    QTest::newRow("proc")
        << "23 66 0:21 / /proc rw,nosuid,nodev,noexec,relatime shared:12 - proc proc rw\n"_ba
        << MountInfo{"/proc", "proc", "proc", "", makedev(0, 21)};

    // E.g. on Android
    QTest::newRow("rootfs")
        << "618 618 0:1 / / ro,relatime master:1 - rootfs rootfs ro,seclabel\n"_ba
        << MountInfo{"/", "rootfs", "rootfs", "", makedev(0, 1)};

    QTest::newRow("ext4")
        << "47 66 8:3 / /home rw,relatime shared:50 - ext4 /dev/sda3 rw,stripe=32736\n"_ba
        << MountInfo{"/home", "ext4", "/dev/sda3", "", makedev(8, 3)};

    QTest::newRow("empty-optional-field")
        << "23 25 0:22 / /apex rw,nosuid,nodev,noexec,relatime - tmpfs tmpfs rw,seclabel,mode=755\n"_ba
        << MountInfo{"/apex", "tmpfs", "tmpfs", "", makedev(0, 22)};

    QTest::newRow("one-optional-field")
        << "47 66 8:3 / /home rw,relatime shared:50 - ext4 /dev/sda3 rw,stripe=32736\n"_ba
        << MountInfo{"/home", "ext4", "/dev/sda3", "", makedev(8, 3)};

    QTest::newRow("multiple-optional-fields")
        << "47 66 8:3 / /home rw,relatime shared:142 master:111 - ext4 /dev/sda3 rw,stripe=32736\n"_ba
        << MountInfo{"/home", "ext4", "/dev/sda3", "", makedev(8, 3)};

    QTest::newRow("mountdir-with-utf8")
        << "129 66 8:51 / /mnt/lab\xC3\xA9l rw,relatime shared:234 - ext4 /dev/sdd3 rw\n"_ba
        << MountInfo{"/mnt/labél", "ext4", "/dev/sdd3", "", makedev(8, 51)};

    QTest::newRow("mountdir-with-space")
        << "129 66 8:51 / /mnt/labe\\040l rw,relatime shared:234 - ext4 /dev/sdd3 rw\n"_ba
        << MountInfo{"/mnt/labe l", "ext4", "/dev/sdd3", "", makedev(8, 51)};

    QTest::newRow("mountdir-with-tab")
        << "129 66 8:51 / /mnt/labe\\011l rw,relatime shared:234 - ext4 /dev/sdd3 rw\n"_ba
        << MountInfo{"/mnt/labe\tl", "ext4", "/dev/sdd3", "", makedev(8, 51)};

    QTest::newRow("mountdir-with-backslash")
        << "129 66 8:51 / /mnt/labe\\134l rw,relatime shared:234 - ext4 /dev/sdd3 rw\n"_ba
        << MountInfo{"/mnt/labe\\l", "ext4", "/dev/sdd3", "", makedev(8, 51)};

    QTest::newRow("mountdir-with-newline")
        << "129 66 8:51 / /mnt/labe\\012l rw,relatime shared:234 - ext4 /dev/sdd3 rw\n"_ba
        << MountInfo{"/mnt/labe\nl", "ext4", "/dev/sdd3", "", makedev(8, 51)};

    QTest::newRow("btrfs-subvol")
        << "775 503 0:49 /foo/bar / rw,relatime shared:142 master:111 - btrfs "
           "/dev/mapper/vg0-stuff rw,ssd,discard,space_cache,subvolid=272,subvol=/foo/bar\n"_ba
        << MountInfo{"/", "btrfs", "/dev/mapper/vg0-stuff", "/foo/bar", makedev(0, 49)};

    QTest::newRow("bind-mount")
        << "59 47 8:17 /rpmbuild /home/user/rpmbuild rw,relatime shared:48 - ext4 /dev/sdb1 rw\n"_ba
        << MountInfo{"/home/user/rpmbuild", "ext4", "/dev/sdb1", "/rpmbuild", makedev(8, 17)};

    QTest::newRow("space-dash-space")
        << "47 66 8:3 / /home\\040-\\040dir rw,relatime shared:50 - ext4 /dev/sda3 rw,stripe=32736\n"_ba
        << MountInfo{"/home - dir", "ext4", "/dev/sda3", "", makedev(8, 3)};

    QTest::newRow("btrfs-mount-bind-file")
        << "1799 1778 0:49 "
            "/var_lib_docker/containers/81fde0fec3dd3d99765c3f7fd9cf1ab121b6ffcfd05d5d7ff434db933fe9d795/resolv.conf "
            "/etc/resolv.conf rw,relatime - btrfs /dev/mapper/vg0-stuff "
            "rw,ssd,discard,space_cache,subvolid=1773,subvol=/var_lib_docker\n"_ba
        << MountInfo{"/etc/resolv.conf", "btrfs", "/dev/mapper/vg0-stuff",
                     "/var_lib_docker/containers/81fde0fec3dd3d99765c3f7fd9cf1ab121b6ffcfd05d5d7ff434db933fe9d795/resolv.conf",
                     makedev(0, 49)};

    QTest::newRow("very-long-line-QTBUG-77059")
        << "727 26 0:52 / "
           "/var/lib/docker/overlay2/f3fbad5eedef71145f00729f0826ea8c44defcfec8c92c58aee0aa2c5ea3fa3a/merged "
           "rw,relatime shared:399 - overlay overlay "
           "rw,lowerdir=/var/lib/docker/overlay2/l/PUP2PIY4EQLAOEDQOZ56BHVE53:"
           "/var/lib/docker/overlay2/l/6IIID3C6J3SUXZEA3GJXKQSTLD:"
           "/var/lib/docker/overlay2/l/PA6N6URNR7XDBBGGOSFWSFQ2CG:"
           "/var/lib/docker/overlay2/l/5EOMBTZNCPOCE4LM3I4JCTNSTT:"
           "/var/lib/docker/overlay2/l/DAMINQ46P3LKX2GDDDIWQKDIWC:"
           "/var/lib/docker/overlay2/l/DHR3N57AEH4OG5QER5XJW2LXIN:"
           "/var/lib/docker/overlay2/l/NW26KA7QPRS2KSVQI77QJWLMHW,"
           "upperdir=/var/lib/docker/overlay2/f3fbad5eedef71145f00729f0826ea8c44defcfec8c92c58aee0aa2c5ea3fa3a/diff,"
           "workdir=/var/lib/docker/overlay2/f3fbad5eedef71145f00729f0826ea8c44defcfec8c92c58aee0aa2c5ea3fa3a/work,"
           "index=off,xino=off\n"_ba
        << MountInfo{"/var/lib/docker/overlay2/f3fbad5eedef71145f00729f0826ea8c44defcfec8c92c58aee0aa2c5ea3fa3a/merged",
                     "overlay", "overlay", "", makedev(0, 52)};

    QTest::newRow("sshfs-src-device-not-start-with-slash")
        << "128 92 0:64 / /mnt-point rw,nosuid,nodev,relatime shared:234 - "
           "fuse.sshfs admin@192.168.1.2:/storage/emulated/0 rw,user_id=1000,group_id=1000\n"_ba
        << MountInfo{"/mnt-point", "fuse.sshfs",
                     "admin@192.168.1.2:/storage/emulated/0", "", makedev(0, 64)};
}

void tst_QStorageInfo::testParseMountInfo()
{
    QFETCH(QByteArray, line);
    QFETCH(MountInfo, expected);

    const std::vector<MountInfo> result = doParseMountInfo(line);
    QVERIFY(!result.empty());
    const MountInfo &a = result.front();
    QCOMPARE(a.mountPoint, expected.mountPoint);
    QCOMPARE(a.fsType, expected.fsType);
    QCOMPARE(a.device, expected.device);
    QCOMPARE(a.fsRoot, expected.fsRoot);
    QCOMPARE(a.stDev, expected.stDev);
}

void tst_QStorageInfo::testParseMountInfo_filtered_data()
{
    QTest::addColumn<QByteArray>("line");

    QTest::newRow("proc")
        << "23 66 0:21 / /proc rw,nosuid,nodev,noexec,relatime shared:12 - proc proc rw\n"_ba;

    QTest::newRow("sys")
        << "24 66 0:22 / /sys rw,nosuid,nodev,noexec,relatime shared:2 - sysfs sysfs rw\n"_ba;
    QTest::newRow("sys-kernel")
        << "26 24 0:6 / /sys/kernel/security rw,nosuid,nodev,noexec,relatime "
           "shared:3 - securityfs securityfs rw\n"_ba;

    QTest::newRow("dev")
        << "25 66 0:5 / /dev rw,nosuid shared:8 - devtmpfs devtmpfs "
           "rw,size=4096k,nr_inodes=8213017,mode=755,inode64\n"_ba;
    QTest::newRow("dev-shm")
            << "27 25 0:23 / /dev/shm rw,nosuid,nodev shared:9 - tmpfs tmpfs rw,inode64\n"_ba;

    QTest::newRow("var-run")
        << "46 28 0:25 / /var/run rw,nosuid,nodev,noexec,relatime shared:1 - "
           "tmpfs tmpfs rw,size=32768k,mode=755,inode64\n"_ba;
    QTest::newRow("var-lock")
        << "46 28 0:25 / /var/lock rw,nosuid,nodev,noexec,relatime shared:1 - "
           "tmpfs tmpfs rw,size=32768k,mode=755,inode64\n"_ba;
}
void tst_QStorageInfo::testParseMountInfo_filtered()
{
    QFETCH(QByteArray, line);
    QVERIFY(doParseMountInfo(line, FilterMountInfo::Filtered).empty());
}

#endif // Q_OS_LINUX

QTEST_MAIN(tst_QStorageInfo)

#include "tst_qstorageinfo.moc"
