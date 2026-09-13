#include "FirmwareUpgradeDialog.h"

#include "IntelHexParser.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

FirmwareUpgradeDialog::FirmwareUpgradeDialog(QWidget *parent)
    : QDialog(parent),
      m_transport(this),
      m_controller(&m_transport, this)
{
    setWindowTitle(QStringLiteral("MYFOC 固件升级工具"));
    setMinimumSize(820, 760);

    auto *title = new QLabel(QStringLiteral("MYFOC 固件升级"), this);
    title->setStyleSheet(QStringLiteral(
        "font-size: 22px; font-weight: 600; color: #172033;"));
    auto *subtitle = new QLabel(
        QStringLiteral("读取 HEX 固件身份，核对设备兼容性，再通过统一 WinUSB 接口升级"),
        this);
    subtitle->setStyleSheet(QStringLiteral("color: #64748b;"));

    auto *deviceGroup = new QGroupBox(QStringLiteral("1. 选择设备"), this);
    m_deviceCombo = new QComboBox(deviceGroup);
    m_deviceCombo->setObjectName(QStringLiteral("deviceCombo"));
    m_deviceCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_refreshButton = new QPushButton(QStringLiteral("刷新设备"), deviceGroup);
    m_refreshButton->setObjectName(QStringLiteral("refreshButton"));
    auto *deviceLayout = new QHBoxLayout(deviceGroup);
    deviceLayout->addWidget(m_deviceCombo, 1);
    deviceLayout->addWidget(m_refreshButton);

    auto *fileGroup = new QGroupBox(QStringLiteral("2. 选择并检查固件"), this);
    m_filePathEdit = new QLineEdit(fileGroup);
    m_filePathEdit->setObjectName(QStringLiteral("firmwarePath"));
    m_filePathEdit->setReadOnly(true);
    m_filePathEdit->setPlaceholderText(QStringLiteral("请选择 .myaes 加密固件或 HEX 文件"));
    m_browseButton = new QPushButton(QStringLiteral("浏览..."), fileGroup);
    m_browseButton->setObjectName(QStringLiteral("browseButton"));
    m_fileSummaryLabel = new QLabel(QStringLiteral("尚未加载固件"), fileGroup);
    m_fileSummaryLabel->setWordWrap(true);
    m_fileSummaryLabel->setStyleSheet(QStringLiteral("color: #64748b;"));
    auto *fileTop = new QHBoxLayout;
    fileTop->addWidget(m_filePathEdit, 1);
    fileTop->addWidget(m_browseButton);
    auto *fileLayout = new QVBoxLayout(fileGroup);
    fileLayout->addLayout(fileTop);
    fileLayout->addWidget(m_fileSummaryLabel);

    auto *informationGroup = new QGroupBox(
        QStringLiteral("3. 固件身份与兼容性"), this);
    auto *candidateTitle = new QLabel(QStringLiteral("候选固件"), informationGroup);
    candidateTitle->setStyleSheet(QStringLiteral("font-weight: 600;"));
    m_candidateInfoLabel = new QLabel(QStringLiteral("尚未选择 HEX"), informationGroup);
    m_candidateInfoLabel->setObjectName(QStringLiteral("candidateFirmwareInfo"));
    m_candidateInfoLabel->setWordWrap(true);
    m_candidateInfoLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto *deviceTitle = new QLabel(QStringLiteral("设备与当前固件"), informationGroup);
    deviceTitle->setStyleSheet(QStringLiteral("font-weight: 600;"));
    m_deviceInfoLabel = new QLabel(
        QStringLiteral("开始升级时查询 GET_DEVICE_INFO / GET_FIRMWARE_INFO"),
        informationGroup);
    m_deviceInfoLabel->setObjectName(QStringLiteral("deviceFirmwareInfo"));
    m_deviceInfoLabel->setWordWrap(true);
    m_deviceInfoLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_compatibilityLabel = new QLabel(
        QStringLiteral("兼容性：等待固件和设备信息"), informationGroup);
    m_compatibilityLabel->setObjectName(QStringLiteral("compatibilityStatus"));
    m_compatibilityLabel->setWordWrap(true);
    m_allowDowngradeCheck = new QCheckBox(
        QStringLiteral("允许固件降级（仅本次升级）"), informationGroup);
    m_allowDowngradeCheck->setObjectName(QStringLiteral("allowDowngrade"));
    m_allowDowngradeCheck->setVisible(false);
    auto *informationLayout = new QGridLayout(informationGroup);
    informationLayout->addWidget(candidateTitle, 0, 0);
    informationLayout->addWidget(m_candidateInfoLabel, 1, 0);
    informationLayout->addWidget(deviceTitle, 0, 1);
    informationLayout->addWidget(m_deviceInfoLabel, 1, 1);
    informationLayout->addWidget(m_compatibilityLabel, 2, 0, 1, 2);
    informationLayout->addWidget(m_allowDowngradeCheck, 3, 0, 1, 2);
    informationLayout->setColumnStretch(0, 1);
    informationLayout->setColumnStretch(1, 1);

    auto *progressGroup = new QGroupBox(QStringLiteral("4. 升级进度"), this);
    m_phaseLabel = new QLabel(QStringLiteral("就绪"), progressGroup);
    m_phaseLabel->setStyleSheet(QStringLiteral(
        "font-size: 15px; font-weight: 600; color: #1d4ed8;"));
    m_recoveryLabel = new QLabel(QStringLiteral("暂存/恢复详情：等待 Bootloader STATUS V2"), progressGroup);
    m_recoveryLabel->setObjectName(QStringLiteral("recoveryDetails"));
    m_recoveryLabel->setWordWrap(true);
    m_recoveryLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_progressBar = new QProgressBar(progressGroup);
    m_progressBar->setObjectName(QStringLiteral("upgradeProgress"));
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_bytesLabel = new QLabel(QStringLiteral("0 B / 0 B"), progressGroup);
    m_speedLabel = new QLabel(QStringLiteral("速度：--"), progressGroup);
    m_etaLabel = new QLabel(QStringLiteral("剩余：--"), progressGroup);
    auto *metrics = new QHBoxLayout;
    metrics->addWidget(m_bytesLabel);
    metrics->addStretch();
    metrics->addWidget(m_speedLabel);
    metrics->addSpacing(20);
    metrics->addWidget(m_etaLabel);
    auto *progressLayout = new QVBoxLayout(progressGroup);
    progressLayout->addWidget(m_phaseLabel);
    progressLayout->addWidget(m_recoveryLabel);
    progressLayout->addWidget(m_progressBar);
    progressLayout->addLayout(metrics);

    auto *logLabel = new QLabel(QStringLiteral("事件记录"), this);
    m_logEdit = new QPlainTextEdit(this);
    m_logEdit->setObjectName(QStringLiteral("eventLog"));
    m_logEdit->setReadOnly(true);
    m_logEdit->setMaximumBlockCount(500);

    m_startButton = new QPushButton(QStringLiteral("开始升级"), this);
    m_startButton->setObjectName(QStringLiteral("startButton"));
    m_startButton->setDefault(true);
    m_startButton->setEnabled(false);
    m_startButton->setMinimumHeight(36);
    m_cancelButton = new QPushButton(QStringLiteral("取消"), this);
    m_cancelButton->setObjectName(QStringLiteral("cancelButton"));
    m_cancelButton->setEnabled(false);
    m_cancelButton->setMinimumHeight(36);
    auto *buttons = new QHBoxLayout;
    buttons->addStretch();
    buttons->addWidget(m_cancelButton);
    buttons->addWidget(m_startButton);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(22, 20, 22, 20);
    layout->setSpacing(10);
    layout->addWidget(title);
    layout->addWidget(subtitle);
    layout->addWidget(deviceGroup);
    layout->addWidget(fileGroup);
    layout->addWidget(informationGroup);
    layout->addWidget(progressGroup);
    layout->addWidget(logLabel);
    layout->addWidget(m_logEdit, 1);
    layout->addLayout(buttons);

    setStyleSheet(QStringLiteral(
        "QDialog { background: #f8fafc; }"
        "QGroupBox { font-weight: 600; border: 1px solid #cbd5e1;"
        " border-radius: 7px; margin-top: 9px; padding-top: 10px;"
        " background: white; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px;"
        " padding: 0 5px; }"
        "QPushButton { padding: 6px 14px; }"
        "QPushButton#startButton { background: #2563eb; color: white;"
        " border: none; border-radius: 5px; font-weight: 600; }"
        "QPushButton#startButton:disabled { background: #94a3b8; }"
        "QProgressBar { border: 1px solid #cbd5e1; border-radius: 5px;"
        " text-align: center; height: 22px; background: #e2e8f0; }"
        "QProgressBar::chunk { background: #22c55e; border-radius: 4px; }"));

    connect(m_refreshButton, &QPushButton::clicked,
            this, &FirmwareUpgradeDialog::refreshDevices);
    connect(m_browseButton, &QPushButton::clicked,
            this, &FirmwareUpgradeDialog::browseFirmware);
    connect(m_startButton, &QPushButton::clicked,
            this, &FirmwareUpgradeDialog::startUpgrade);
    connect(m_cancelButton, &QPushButton::clicked,
            this, &FirmwareUpgradeDialog::cancelUpgrade);
    connect(m_allowDowngradeCheck, &QCheckBox::toggled,
            &m_controller, &FirmwareUpgradeController::setAllowDowngrade);
    connect(&m_controller, &FirmwareUpgradeController::devicesChanged,
            this, &FirmwareUpgradeDialog::updateDevices);
    connect(&m_controller, &FirmwareUpgradeController::stageChanged,
            this, &FirmwareUpgradeDialog::updateStage);
    connect(&m_controller, &FirmwareUpgradeController::progressChanged,
            this, &FirmwareUpgradeDialog::updateProgress);
    connect(&m_controller,
            &FirmwareUpgradeController::deviceInformationChanged,
            this, &FirmwareUpgradeDialog::updateDeviceInformation);
    connect(&m_controller, &FirmwareUpgradeController::finished,
            this, &FirmwareUpgradeDialog::upgradeFinished);
    connect(&m_controller, &FirmwareUpgradeController::logMessage,
            this, &FirmwareUpgradeDialog::appendLog);
    connect(&m_controller, &FirmwareUpgradeController::recoveryStatusChanged,
            this, &FirmwareUpgradeDialog::updateRecoveryStatus);

    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setInterval(2000);
    connect(m_refreshTimer, &QTimer::timeout,
            this, &FirmwareUpgradeDialog::refreshDevices);
    m_refreshTimer->start();
    QTimer::singleShot(0, this, &FirmwareUpgradeDialog::refreshDevices);
}

void FirmwareUpgradeDialog::browseFirmware()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("选择升级固件"), QString(),
        QStringLiteral("加密固件 (*.myaes);;Intel HEX 固件 (*.hex *.ihx);;所有文件 (*.*)"));
    if (path.isEmpty())
        return;
    FirmwareImage image;
    QString error;
    if (!IntelHexParser::parseFile(path, &image, &error)) {
        m_image = {};
        m_firmwarePath.clear();
        m_filePathEdit->clear();
        m_fileSummaryLabel->setText(QStringLiteral("固件无效：%1").arg(error));
        m_candidateInfoLabel->setText(QStringLiteral("无法解析固件身份"));
        appendLog(QStringLiteral("固件校验失败：%1").arg(error));
        m_startButton->setEnabled(false);
        return;
    }
    m_image = image;
    m_firmwarePath = path;
    m_allowDowngradeCheck->setChecked(false);
    m_allowDowngradeCheck->setVisible(false);
    m_filePathEdit->setText(QDir::toNativeSeparators(path));
    m_fileSummaryLabel->setText(
        QStringLiteral("%1 · %2 · 地址 0x%3 · CRC32 %4")
            .arg(QFileInfo(path).fileName())
            .arg(formatBytes(quint32(image.image.size())))
            .arg(image.baseAddress, 8, 16, QLatin1Char('0'))
            .arg(image.crc32, 8, 16, QLatin1Char('0')).toUpper());
    m_candidateInfoLabel->setText(formatFirmwareInfo(image.firmwareInfo));
    m_compatibilityLabel->setText(
        QStringLiteral("兼容性：点击开始升级后读取设备身份并判断"));
    const QString warning = image.firmwareInfo.isDirty()
        ? QStringLiteral("（警告：dirty 调试构建）")
        : (image.firmwareInfo.isDebugBuild()
           ? QStringLiteral("（警告：Debug 构建）") : QString());
    if (!warning.isEmpty())
        m_candidateInfoLabel->setText(
            m_candidateInfoLabel->text() + QStringLiteral("\n") + warning);
    appendLog(QStringLiteral("已加载固件：%1").arg(path));
    m_startButton->setEnabled(!m_devices.isEmpty()
                              && !m_controller.isActive());
}

void FirmwareUpgradeDialog::refreshDevices()
{
    if (!m_controller.isActive())
        m_controller.refreshDevices();
}

void FirmwareUpgradeDialog::updateDevices(const QList<UpgradeDevice> &devices)
{
    const int oldIndex = m_deviceCombo->currentIndex();
    QString oldPath;
    if (oldIndex >= 0 && oldIndex < m_devices.size())
        oldPath = m_devices.at(oldIndex).path;
    m_devices = devices;
    m_deviceCombo->clear();
    int restoreIndex = -1;
    for (int index = 0; index < devices.size(); ++index) {
        m_deviceCombo->addItem(devices.at(index).displayName());
        if (devices.at(index).path == oldPath)
            restoreIndex = index;
    }
    if (restoreIndex >= 0)
        m_deviceCombo->setCurrentIndex(restoreIndex);
    if (devices.isEmpty())
        m_deviceCombo->addItem(QStringLiteral("未发现可升级设备"));
    m_startButton->setEnabled(!devices.isEmpty() && !m_image.image.isEmpty()
                              && !m_controller.isActive());
}

void FirmwareUpgradeDialog::startUpgrade()
{
    const int index = m_deviceCombo->currentIndex();
    if (index < 0 || index >= m_devices.size() || m_image.image.isEmpty())
        return;
    m_controller.setAllowDowngrade(m_allowDowngradeCheck->isChecked());
    m_progressBar->setRange(0, m_image.image.size());
    m_progressBar->setValue(0);
    m_bytesLabel->setText(
        QStringLiteral("0 B / %1").arg(formatBytes(m_image.image.size())));
    m_speedLabel->setText(QStringLiteral("速度：--"));
    m_etaLabel->setText(QStringLiteral("剩余：--"));
    m_phaseLabel->setStyleSheet(QStringLiteral(
        "font-size: 15px; font-weight: 600; color: #1d4ed8;"));
    setUpgradeActive(true);
    appendLog(QStringLiteral("开始升级设备 %1")
              .arg(m_devices.at(index).displayName()));
    m_controller.startUpgrade(m_devices.at(index), m_image);
}

void FirmwareUpgradeDialog::cancelUpgrade()
{
    m_controller.cancel();
}

void FirmwareUpgradeDialog::updateStage(
    FirmwareUpgradeController::Stage, const QString &description)
{
    m_phaseLabel->setText(description);
}

void FirmwareUpgradeDialog::updateProgress(
    quint32 acknowledged, quint32 total,
    double bytesPerSecond, int etaSeconds)
{
    m_progressBar->setRange(0, int(total));
    m_progressBar->setValue(int(acknowledged));
    m_progressBar->setFormat(QStringLiteral("%p%"));
    m_bytesLabel->setText(QStringLiteral("%1 / %2")
                         .arg(formatBytes(acknowledged))
                         .arg(formatBytes(total)));
    m_speedLabel->setText(QStringLiteral("速度：%1 KiB/s")
                         .arg(bytesPerSecond / 1024.0, 0, 'f', 1));
    m_etaLabel->setText(QStringLiteral("剩余：%1 秒").arg(etaSeconds));
}

void FirmwareUpgradeDialog::updateRecoveryStatus(
    quint8 phase, quint8 activeSlot, quint8 candidateSlot,
    const QByteArray &packageId, quint32 downloadOffset, quint32 backupOffset,
    quint32 installOffset, quint16 lastError)
{
    static const QStringList phaseNames = {
        QStringLiteral("空"), QStringLiteral("APP 有效"), QStringLiteral("下载到 W25Q128"),
        QStringLiteral("候选已暂存"), QStringLiteral("备份旧 APP"),
        QStringLiteral("备份完成"), QStringLiteral("写入 STM32 内部 Flash"),
        QStringLiteral("校验内部 Flash"), QStringLiteral("回滚旧 APP"), QStringLiteral("错误")};
    const QString phaseText = phase < phaseNames.size() ? phaseNames.at(phase)
                                                         : QStringLiteral("未知");
    m_recoveryLabel->setText(QStringLiteral("阶段：%1 · Active 槽：%2 · Candidate 槽：%3 · packageId：%4\nW25Q128 下载：%5 B · 旧 APP 备份：%6 B · STM32 内部 Flash：%7 B · 错误：0x%8")
        .arg(phaseText).arg(activeSlot).arg(candidateSlot)
        .arg(QString::fromLatin1(packageId.toHex().toUpper()))
        .arg(downloadOffset).arg(backupOffset).arg(installOffset)
        .arg(lastError, 4, 16, QLatin1Char('0')).toUpper());
}

void FirmwareUpgradeDialog::updateDeviceInformation(
    const DeviceInfo &device, const FirmwareInfo &installed,
    bool installedValid, const QString &compatibility,
    bool upgradeAllowed, bool downgrade)
{
    const QString mode = device.mode == App1Codec::ApplicationMode
        ? QStringLiteral("Application") : QStringLiteral("Bootloader");
    QString text = QStringLiteral(
        "模式：%1\n产品 ID：0x%2\n硬件版本：%3\nUID：%4")
        .arg(mode)
        .arg(device.productId, 8, 16, QLatin1Char('0'))
        .arg(device.hardwareRevision)
        .arg(QString::fromLatin1(device.uniqueId.toHex().toUpper()));
    if (installedValid)
        text += QStringLiteral("\n当前固件：%1 · build %2 · %3")
            .arg(installed.versionString())
            .arg(installed.buildNumber)
            .arg(installed.gitCommit);
    else
        text += QStringLiteral("\n当前固件：无有效 APP");
    m_deviceInfoLabel->setText(text);
    m_compatibilityLabel->setText(
        QStringLiteral("兼容性：%1").arg(compatibility));
    m_compatibilityLabel->setStyleSheet(upgradeAllowed
        ? QStringLiteral("color: #15803d; font-weight: 600;")
        : QStringLiteral("color: #b45309; font-weight: 600;"));
    m_allowDowngradeCheck->setVisible(downgrade);
    m_allowDowngradeCheck->setEnabled(downgrade && !m_controller.isActive());
}

void FirmwareUpgradeDialog::upgradeFinished(bool success,
                                             const QString &message)
{
    setUpgradeActive(false);
    m_phaseLabel->setStyleSheet(success
        ? QStringLiteral("font-size: 15px; font-weight: 600; color: #15803d;")
        : QStringLiteral("font-size: 15px; font-weight: 600; color: #b91c1c;"));
    appendLog(message);
    if (success)
        m_progressBar->setValue(m_progressBar->maximum());
    refreshDevices();
}

void FirmwareUpgradeDialog::appendLog(const QString &message)
{
    m_logEdit->appendPlainText(QStringLiteral("[%1] %2")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")),
             message));
}

void FirmwareUpgradeDialog::setUpgradeActive(bool active)
{
    if (active)
        m_refreshTimer->stop();
    else
        m_refreshTimer->start();
    m_deviceCombo->setEnabled(!active);
    m_refreshButton->setEnabled(!active);
    m_browseButton->setEnabled(!active);
    m_allowDowngradeCheck->setEnabled(!active
                                      && m_allowDowngradeCheck->isVisible());
    m_startButton->setEnabled(!active && !m_devices.isEmpty()
                              && !m_image.image.isEmpty());
    m_cancelButton->setEnabled(active);
}

QString FirmwareUpgradeDialog::formatBytes(quint32 bytes)
{
    if (bytes >= 1024U * 1024U)
        return QStringLiteral("%1 MiB")
            .arg(double(bytes) / (1024.0 * 1024.0), 0, 'f', 2);
    if (bytes >= 1024U)
        return QStringLiteral("%1 KiB")
            .arg(double(bytes) / 1024.0, 0, 'f', 1);
    return QStringLiteral("%1 B").arg(bytes);
}

QString FirmwareUpgradeDialog::formatFirmwareInfo(const FirmwareInfo &info)
{
    const QString utc = QDateTime::fromSecsSinceEpoch(
        qint64(info.buildTimeUtc), Qt::UTC).toString(Qt::ISODate);
    return QStringLiteral(
        "%1 / %2\n%3  v%4  build %5\n产品 ID：0x%6  硬件：%7–%8\n"
        "%9 · Git %10 · UTC %11\n镜像：0x%12  向量：0x%13")
        .arg(info.vendorName, info.deviceName, info.firmwareName,
             info.versionString())
        .arg(info.buildNumber)
        .arg(info.productId, 8, 16, QLatin1Char('0'))
        .arg(info.hardwareRevisionMinimum)
        .arg(info.hardwareRevisionMaximum)
        .arg(info.buildTypeString(), info.gitCommit, utc)
        .arg(info.imageBase, 8, 16, QLatin1Char('0'))
        .arg(info.vectorBase, 8, 16, QLatin1Char('0'));
}
