#include <cstdlib>
#include <QApplication>
#include <QWidget>
#include <QMenu>
#include <QIcon>
#include <QSystemTrayIcon>
#include <QDBusServiceWatcher>
#include <QMessageBox>

#include "warumbus.h"

class WarumTray : public QWidget
{
    Q_OBJECT

public:
    WarumTray(QWidget *parent = 0);
    ~WarumTray();
private:
    enum WarumTrayState { Disabled, Enabled, NotRunning };
    QIcon m_iconEnabled;
    QIcon m_iconDisabled;
    QMenu *m_trayMenu;
    QAction *m_firstmenuItem;
    QSystemTrayIcon *m_systemTray;
    PkQwerty12WarumInterface *m_remoteWarum;
    OrgFreedesktopDBusPropertiesInterface *m_remotewarumpropIface;
    QDBusServiceWatcher *m_serviceWatcher;
private:
    void setupIcons();
    void setupMenu();
    void setState(WarumTrayState);
private slots:
    void onServiceRegistered(const QString &);
    void onServiceUnregistered(const QString &);
    void onPropertiesChanged(const QString &, const QVariantMap &, const QStringList &);
    void onFirstMenuitemTriggered();
    void onTrayActivated(QSystemTrayIcon::ActivationReason);
};

#include "warumtray.moc"

void WarumTray::setState(WarumTrayState state)
{
    switch (state)
    {
        case Disabled:
            m_firstmenuItem->setDisabled(false);
            m_firstmenuItem->setText("&Start");
            m_systemTray->setToolTip("Warum disabled");
            m_systemTray->setIcon(m_iconDisabled);
            break;
        case Enabled:
            m_firstmenuItem->setDisabled(false);
            m_firstmenuItem->setText("&Stop");
            m_systemTray->setToolTip("Warum enabled");
            m_systemTray->setIcon(m_iconEnabled);
            break;
        case NotRunning: {
            const QString &disabledStr = "Warum not running";
            m_firstmenuItem->setDisabled(true);
            m_firstmenuItem->setText(disabledStr);
            m_systemTray->setToolTip(disabledStr);
            m_systemTray->setIcon(m_iconDisabled);
            break;
        }
    }
}

void WarumTray::onPropertiesChanged(const QString &interface_name, const QVariantMap &changed_properties, const QStringList &invalidated_properties)
{
    Q_UNUSED(invalidated_properties);
    if (Q_LIKELY(interface_name == PkQwerty12WarumInterface::staticInterfaceName() && changed_properties.contains("Enabled")))
        setState((WarumTrayState)changed_properties["Enabled"].toBool());
}

void WarumTray::onServiceUnregistered(const QString &serviceName)
{
    if (Q_LIKELY(serviceName == PkQwerty12WarumInterface::staticInterfaceName()))
        setState(NotRunning);
}

void WarumTray::onServiceRegistered(const QString &serviceName)
{
    if (Q_LIKELY(serviceName == PkQwerty12WarumInterface::staticInterfaceName()))
        setState((WarumTrayState)m_remoteWarum->enabled());
}

void WarumTray::onFirstMenuitemTriggered()
{
    m_firstmenuItem->setDisabled(true);
    m_remoteWarum->setEnabled(!m_remoteWarum->enabled());
}

void WarumTray::onTrayActivated(QSystemTrayIcon::ActivationReason reason)
{
    if (reason == QSystemTrayIcon::MiddleClick && m_remoteWarum->isValid())
        onFirstMenuitemTriggered();
}

void WarumTray::setupIcons()
{
    m_iconEnabled = QIcon::fromTheme("globe");
    if (!m_iconEnabled.isNull()) {
        // https://stackoverflow.com/a/42318289
        int w = m_iconEnabled.availableSizes().last().width();
        int h = m_iconEnabled.availableSizes().last().height();

        QImage im = m_iconEnabled.pixmap(w, h).toImage().convertToFormat(QImage::Format_ARGB32);
        for (int y = 0; y < im.height(); ++y) {
            auto *scanLine = (QRgb*) im.scanLine(y);
            for (int x = 0; x < im.width(); ++x) {
                QRgb pixel = *scanLine;
                auto ci = uint(qGray(pixel));
                *scanLine = qRgba(ci, ci, ci, qAlpha(pixel)/3);
                ++scanLine;
            }
        }
        m_iconDisabled = QPixmap::fromImage(im);
    }
}

void WarumTray::setupMenu()
{
    m_firstmenuItem = m_trayMenu->addAction("");
    m_trayMenu->addSeparator();
    connect(m_trayMenu->addAction("E&xit"), &QAction::triggered, this, &QApplication::quit);
    connect(m_firstmenuItem, &QAction::triggered, this, &WarumTray::onFirstMenuitemTriggered);

    m_systemTray->setContextMenu(m_trayMenu);
}

WarumTray::WarumTray(QWidget *parent)
    : QWidget(parent),
      m_trayMenu(new QMenu(this)),
      m_systemTray(new QSystemTrayIcon(this)),
      m_remoteWarum(new PkQwerty12WarumInterface(PkQwerty12WarumInterface::staticInterfaceName(), "/", QDBusConnection::systemBus(), this)),
      m_remotewarumpropIface(new OrgFreedesktopDBusPropertiesInterface(PkQwerty12WarumInterface::staticInterfaceName(), "/", QDBusConnection::systemBus(), this)),
      m_serviceWatcher(new QDBusServiceWatcher(PkQwerty12WarumInterface::staticInterfaceName(), QDBusConnection::systemBus(), QDBusServiceWatcher::WatchForRegistration | QDBusServiceWatcher::WatchForUnregistration, this))
{
    setupIcons();
    setupMenu();
    setState(NotRunning);

    if (m_remoteWarum->isValid())
        onServiceRegistered(PkQwerty12WarumInterface::staticInterfaceName());

    connect(m_serviceWatcher, &QDBusServiceWatcher::serviceRegistered, this, &WarumTray::onServiceRegistered);
    connect(m_serviceWatcher, &QDBusServiceWatcher::serviceUnregistered, this, &WarumTray::onServiceUnregistered);
    connect(m_remotewarumpropIface, &OrgFreedesktopDBusPropertiesInterface::PropertiesChanged, this, &WarumTray::onPropertiesChanged);
    connect(m_systemTray, &QSystemTrayIcon::activated, this, &WarumTray::onTrayActivated);

    m_systemTray->show();
}

WarumTray::~WarumTray()
{
}

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    if (!QDBusConnection::systemBus().isConnected()) {
        QMessageBox::critical(nullptr, nullptr, "Cannot connect to the D-Bus system bus.", QMessageBox::Close);
        return EXIT_FAILURE;
    }
    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        QMessageBox::critical(nullptr, nullptr, "System tray not available.", QMessageBox::Close);
        return EXIT_FAILURE;
    }
    WarumTray w;

    return a.exec();
}
