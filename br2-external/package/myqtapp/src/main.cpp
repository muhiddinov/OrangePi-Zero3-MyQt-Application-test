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

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QElapsedTimer bootTimer;
    bootTimer.start();

    QString videoSrc = QString::fromLocal8Bit(qgetenv("VIDEO_SRC"));
    if (videoSrc.isEmpty())
        videoSrc = QStringLiteral("/root/myvideo.mp4");

    QWidget window;
    window.setStyleSheet("background-color: black;");

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
    player.setMedia(QUrl::fromLocalFile(videoSrc));

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
            player.setPosition(0);
            player.play();
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

    qDebug() << "[t=" << bootTimer.elapsed() << "ms] splash shown, starting playback";
    player.play();

    return app.exec();
}
