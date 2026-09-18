#include <QApplication>
#include <QMediaPlayer>
#include <QVideoWidget>
#include <QPixmap>
#include <QPainter>
#include <QUrl>
#include <QDebug>
#include <QTimer>
#include <QStackedLayout>
#include <QVariantAnimation>
#include <QElapsedTimer>
#include <QScreen>
#include <QDir>
#include <QFileInfo>
#include <QFile>

static const QString kUsbMountPoint = QStringLiteral("/mnt/usb");

class SplashWidget : public QWidget
{
public:
    using QWidget::QWidget;

    void setLogo(const QPixmap &pixmap) { m_logo = pixmap; update(); }
    void setFadeOpacity(qreal opacity) { m_opacity = opacity; update(); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setOpacity(m_opacity);
        painter.fillRect(rect(), Qt::white);
        if (!m_logo.isNull()) {
            QPoint pos((width() - m_logo.width()) / 2, (height() - m_logo.height()) / 2);
            painter.drawPixmap(pos, m_logo);
        }
    }

private:
    QPixmap m_logo;
    qreal m_opacity = 1.0;
};

static bool isUsbMounted()
{
    QFile mounts(QStringLiteral("/proc/mounts"));
    if (!mounts.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;
    const QByteArray data = mounts.readAll();
    for (const QByteArray &line : data.split('\n')) {
        const QList<QByteArray> fields = line.split(' ');
        if (fields.size() >= 2 && fields.at(1) == kUsbMountPoint.toUtf8())
            return true;
    }
    return false;
}

static QStringList findMp4Files(const QString &dirPath)
{
    QStringList result;
    QDir dir(dirPath);
    const QFileInfoList entries = dir.entryInfoList(QDir::Files, QDir::Name);
    for (const QFileInfo &fi : entries) {
        if (fi.suffix().compare(QStringLiteral("mp4"), Qt::CaseInsensitive) == 0)
            result << fi.absoluteFilePath();
    }
    return result;
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QElapsedTimer bootTimer;
    bootTimer.start();

    QString defaultVideoSrc = QString::fromLocal8Bit(qgetenv("VIDEO_SRC"));
    if (defaultVideoSrc.isEmpty())
        defaultVideoSrc = QStringLiteral("/root/myvideo.mp4");

    QWidget window;
    window.setStyleSheet("background-color: black;");
    // Set the exact screen geometry before the first show/paint - without
    // this, the window briefly paints at Qt's default fallback size
    // (positioned in the top-left corner) before the fullscreen resize
    // takes effect, which is visible as a small white/black flash there
    // during myqtapp's (slow, dynamic-linking-heavy) startup.
    window.setGeometry(QGuiApplication::primaryScreen()->geometry());

    auto *stack = new QStackedLayout(&window);
    stack->setStackingMode(QStackedLayout::StackAll);
    stack->setContentsMargins(0, 0, 0, 0);

    auto *videoWidget = new QVideoWidget;
    videoWidget->setStyleSheet("background-color: black;");
    stack->addWidget(videoWidget);

    auto *splash = new SplashWidget;
    stack->addWidget(splash);

    window.showFullScreen();
    app.processEvents();

    QPixmap logo(":/payzone-logo.png");
    splash->setLogo(logo.scaledToWidth(window.width() * 6 / 10, Qt::SmoothTransformation));
    splash->raise();

    QMediaPlayer player;
    player.setVideoOutput(videoWidget);

    // QMediaPlaylist's own Loop/auto-advance was unreliable on this board's
    // GStreamer backend (re-opening the next/same source right after
    // EndOfMedia intermittently failed with "Internal data stream error",
    // even though the same file played back fine standalone via
    // gst-launch-1.0, twice in a row). Advancing and looping the list
    // manually - one setMedia()+play() call at a time - is what already
    // worked reliably for the single-file case, so that's kept here too.
    QStringList currentPlaylistFiles;
    int currentIndex = 0;

    auto playCurrent = [&]() {
        player.setMedia(QUrl::fromLocalFile(currentPlaylistFiles.at(currentIndex)));
        player.play();
    };

    // Checks for a mounted USB drive with *.mp4 files on it; falls back to
    // the default video (VIDEO_SRC or /root/myvideo.mp4) otherwise. Called
    // on startup and periodically, so plugging/unplugging a USB drive at
    // runtime switches the playlist without needing a restart.
    auto refreshPlaylist = [&]() {
        QStringList files;
        if (isUsbMounted())
            files = findMp4Files(kUsbMountPoint);
        if (files.isEmpty())
            files << defaultVideoSrc;

        if (files == currentPlaylistFiles)
            return;

        qDebug() << "[t=" << bootTimer.elapsed() << "ms] playlist changed:" << files;
        currentPlaylistFiles = files;
        currentIndex = 0;
        playCurrent();
    };

    bool splashMinTimeElapsed = false;
    bool mediaReady = false;
    bool transitioning = false;
    auto trySwitchToVideo = [&]() {
        if (transitioning || !splashMinTimeElapsed || !mediaReady)
            return;
        transitioning = true;
        qDebug() << "[t=" << bootTimer.elapsed() << "ms] starting fade-out";

        auto *fadeOut = new QVariantAnimation(&app);
        fadeOut->setDuration(1000);
        fadeOut->setStartValue(1.0);
        fadeOut->setEndValue(0.0);
        QObject::connect(fadeOut, &QVariantAnimation::valueChanged, [&bootTimer, splash](const QVariant &v) {
            qDebug() << "[t=" << bootTimer.elapsed() << "ms] opacity =" << v.toDouble();
            splash->setFadeOpacity(v.toDouble());
        });
        QObject::connect(fadeOut, &QVariantAnimation::finished, [&bootTimer, splash]() {
            qDebug() << "[t=" << bootTimer.elapsed() << "ms] fade-out finished";
            splash->hide();
        });
        fadeOut->start(QAbstractAnimation::DeleteWhenStopped);
    };

    QObject::connect(&player, QOverload<QMediaPlayer::Error>::of(&QMediaPlayer::error), [&](QMediaPlayer::Error) {
        qWarning() << "MediaPlayer error:" << player.errorString();
    });
    QObject::connect(&player, &QMediaPlayer::mediaStatusChanged, [&](QMediaPlayer::MediaStatus status) {
        qDebug() << "MediaPlayer status:" << status;
        if (!transitioning)
            splash->raise();
        if (status == QMediaPlayer::BufferedMedia || status == QMediaPlayer::BufferingMedia) {
            mediaReady = true;
            trySwitchToVideo();
        }
        if (status == QMediaPlayer::EndOfMedia) {
            currentIndex = (currentIndex + 1) % currentPlaylistFiles.size();
            playCurrent();
        }
    });

    QTimer::singleShot(5000, [&]() {
        qDebug() << "[t=" << bootTimer.elapsed() << "ms] 5s splash timer fired";
        splashMinTimeElapsed = true;
        trySwitchToVideo();
    });

    auto *keepOnTop = new QTimer(&app);
    QObject::connect(keepOnTop, &QTimer::timeout, [&]() {
        if (transitioning) {
            keepOnTop->stop();
            return;
        }
        splash->raise();
    });
    keepOnTop->start(200);

    auto *usbPollTimer = new QTimer(&app);
    QObject::connect(usbPollTimer, &QTimer::timeout, refreshPlaylist);
    usbPollTimer->start(2000);

    qDebug() << "[t=" << bootTimer.elapsed() << "ms] splash shown, starting playback";
    refreshPlaylist();

    return app.exec();
}
