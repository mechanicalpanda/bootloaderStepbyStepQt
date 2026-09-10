#include "FirmwareUpgradeDialog.h"

#include "IntelHexParser.h"

#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
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
    setMinimumSize(720, 560);

    auto *title = new QLabel(QStringLiteral("MYFOC 固件升级"), this);
    title->setStyleSheet(QStringLiteral(
        "font-size: 22px; font-weight: 600; color: #172033;"));
    auto *subtitle = new QLabel(
        QStringLiteral("通过统一 WinUSB 接口查询运行模式并安全写入 Intel HEX 固件"), this);
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

    auto *fileGroup = new QGroupBox(QStringLiteral("2. 选择固件"), this);
    m_filePathEdit = new QLineEdit(fileGroup);
    m_filePathEdit->setObjectName(QStringLiteral("firmwarePath"));
    m_filePathEdit->setReadOnly(true);
    m_filePathEdit->setPlaceholderText(QStringLiteral("请选择 .hex 或 .ihx 文件"));
    m_browseButton = new QPushButton(QStringLiteral("浏览..."), fileGroup);
    m_browseButton->setObjectName(QStringLiteral("browseButton"));
    m_fileSummaryLabel = new QLabel(
        QStringLiteral("尚未加载固件"), fileGroup);
    m_fileSummaryLabel->setWordWrap(true);
    m_fileSummaryLabel->setStyleSheet(QStringLiteral("color: #64748b;"));
    auto *fileTop = new QHBoxLayout;
    fileTop->addWidget(m_filePathEdit, 1);
    fileTop->addWidget(m_browseButton);
    auto *fileLayout = new QVBoxLayout(fileGroup);
    fileLayout->addLayout(fileTop);
    fileLayout->addWidget(m_fileSummaryLabel);

    auto *progressGroup = new QGroupBox(QStringLiteral("3. 升级进度"), this);
    m_phaseLabel = new QLabel(QStringLiteral("就绪"), progressGroup);
    m_phaseLabel->setStyleSheet(QStringLiteral(
        "font-size: 15px; font-weight: 600; color: #1d4ed8;"));
    m_progressBar = new QProgressBar(progressGroup);
    m_progressBar->setObjectName(QStringLiteral("upgradeProgress"));
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setTextVisible(true);
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
    progressLayout->addWidget(m_progressBar);
    progressLayout->addLayout(metrics);

    auto *logLabel = new QLabel(QStringLiteral("事件记录"), this);
    m_logEdit = new QPlainTextEdit(this);
    m_logEdit->setObjectName(QStringLiteral("eventLog"));
    m_logEdit->setReadOnly(true);
    m_logEdit->setMaximumBlockCount(500);
    m_logEdit->setPlaceholderText(QStringLiteral("设备搜索与升级事件将在这里显示"));

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
    layout->setSpacing(12);
    layout->addWidget(title);
    layout->addWidget(subtitle);
    layout->addWidget(deviceGroup);
    layout->addWidget(fileGroup);
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
    connect(&m_controller, &FirmwareUpgradeController::devicesChanged,
            this, &FirmwareUpgradeDialog::updateDevices);
    connect(&m_controller, &FirmwareUpgradeController::stageChanged,
            this, &FirmwareUpgradeDialog::updateStage);
    connect(&m_controller, &FirmwareUpgradeController::progressChanged,
            this, &FirmwareUpgradeDialog::updateProgress);
    connect(&m_controller, &FirmwareUpgradeController::finished,
            this, &FirmwareUpgradeDialog::upgradeFinished);
    connect(&m_controller, &FirmwareUpgradeController::logMessage,
            this, &FirmwareUpgradeDialog::appendLog);

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
        QStringLiteral("Intel HEX 固件 (*.hex *.ihx);;所有文件 (*.*)"));
    if (path.isEmpty())
        return;
    FirmwareImage image;
    QString error;
    if (!IntelHexParser::parseFile(path, &image, &error)) {
        m_image = {};
        m_firmwarePath.clear();
        m_filePathEdit->clear();
        m_fileSummaryLabel->setText(QStringLiteral("固件无效：%1").arg(error));
        appendLog(QStringLiteral("固件校验失败：%1").arg(error));
        m_startButton->setEnabled(false);
        return;
    }
    m_image = image;
    m_firmwarePath = path;
    m_filePathEdit->setText(QDir::toNativeSeparators(path));
    m_fileSummaryLabel->setText(
        QStringLiteral("%1 · %2 · 地址 0x%3 · CRC32 %4")
            .arg(QFileInfo(path).fileName())
            .arg(formatBytes(quint32(image.image.size())))
            .arg(image.baseAddress, 8, 16, QLatin1Char('0'))
            .arg(image.crc32, 8, 16, QLatin1Char('0'))
            .toUpper());
    appendLog(QStringLiteral("已加载固件：%1").arg(path));
    m_startButton->setEnabled(
        !m_devices.isEmpty() && !m_controller.isActive());
}

void FirmwareUpgradeDialog::refreshDevices()
{
    if (!m_controller.isActive())
        m_controller.refreshDevices();
}

void FirmwareUpgradeDialog::updateDevices(
    const QList<UpgradeDevice> &devices)
{
    const int oldIndex = m_deviceCombo->currentIndex();
    QString oldPath;
    if (oldIndex >= 0 && oldIndex < m_devices.size())
        oldPath = m_devices.at(oldIndex).path;
    m_devices = devices;
    m_deviceCombo->clear();
    int restoreIndex = -1;
    for (int i = 0; i < devices.size(); ++i) {
        m_deviceCombo->addItem(devices.at(i).displayName());
        if (devices.at(i).path == oldPath)
            restoreIndex = i;
    }
    if (restoreIndex >= 0)
        m_deviceCombo->setCurrentIndex(restoreIndex);
    if (devices.isEmpty())
        m_deviceCombo->addItem(QStringLiteral("未发现可升级设备"));
    m_startButton->setEnabled(
        !devices.isEmpty() && !m_image.image.isEmpty()
        && !m_controller.isActive());
}

void FirmwareUpgradeDialog::startUpgrade()
{
    const int index = m_deviceCombo->currentIndex();
    if (index < 0 || index >= m_devices.size() || m_image.image.isEmpty())
        return;
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

void FirmwareUpgradeDialog::upgradeFinished(
    bool success, const QString &message)
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
    m_logEdit->appendPlainText(
        QStringLiteral("[%1] %2")
            .arg(QDateTime::currentDateTime().toString(
                     QStringLiteral("HH:mm:ss")),
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
    m_startButton->setEnabled(
        !active && !m_devices.isEmpty() && !m_image.image.isEmpty());
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
