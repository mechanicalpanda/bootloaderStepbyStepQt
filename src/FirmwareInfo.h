#ifndef FIRMWAREINFO_H
#define FIRMWAREINFO_H

#include <QByteArray>
#include <QString>
#include <QtGlobal>

class FirmwareInfo
{
public:
    enum Flag : quint16
    {
        DirtyFlag = 0x0001U,
        DebugBuildFlag = 0x0002U,
        ReleaseBuildFlag = 0x0004U
    };

    static constexpr int HeaderSize = 256;
    static constexpr quint32 ExpectedImageBase = 0x08020000U;
    static constexpr quint32 ExpectedVectorBase = 0x08020200U;
    static constexpr quint32 ExpectedRegionSize = 0x000C0000U;

    QByteArray raw;
    quint32 productId = 0U;
    quint16 hardwareRevisionMinimum = 0U;
    quint16 hardwareRevisionMaximum = 0U;
    quint16 versionMajor = 0U;
    quint16 versionMinor = 0U;
    quint16 versionPatch = 0U;
    quint16 flags = 0U;
    quint32 buildNumber = 0U;
    quint32 imageBase = 0U;
    quint32 vectorBase = 0U;
    quint32 applicationRegionSize = 0U;
    quint64 buildTimeUtc = 0U;
    QString vendorName;
    QString deviceName;
    QString firmwareName;
    QString gitCommit;

    static bool decode(const QByteArray &bytes, FirmwareInfo *info,
                       QString *error);
    static int compareSemanticVersion(const FirmwareInfo &left,
                                      const FirmwareInfo &right);

    QString versionString() const;
    QString buildTypeString() const;
    bool isDirty() const { return (flags & DirtyFlag) != 0U; }
    bool isDebugBuild() const { return (flags & DebugBuildFlag) != 0U; }
    bool isReleaseBuild() const { return (flags & ReleaseBuildFlag) != 0U; }
    bool sameBuildIdentity(const FirmwareInfo &other) const;
};

#endif // FIRMWAREINFO_H
