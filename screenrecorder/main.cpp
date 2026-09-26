#include "recorder.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QSocketNotifier>

#include <csignal>
#include <cstdio>

#include <sys/signalfd.h>
#include <unistd.h>

// Prints "started" at the first frame; "stop" or the end of standard input ends
// the recording, and the exit status says whether the file was completed.
int main(int argc, char **argv)
{
    // Blocked before any thread exists, so every thread inherits it and the
    // signals arrive only through the signalfd.
    sigset_t blocked;
    sigemptyset(&blocked);
    sigaddset(&blocked, SIGINT);
    sigaddset(&blocked, SIGTERM);
    sigaddset(&blocked, SIGHUP);
    pthread_sigmask(SIG_BLOCK, &blocked, nullptr);
    std::signal(SIGPIPE, SIG_IGN);

    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("cutefish-screenrecorder"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Records the screen to an MP4 file."));
    parser.addHelpOption();
    const QCommandLineOption outputOption(QStringLiteral("output"),
                                          QStringLiteral("Output to record."),
                                          QStringLiteral("name"));
    const QCommandLineOption regionOption(
            QStringLiteral("region"),
            QStringLiteral("Part of the output, in global logical coordinates."),
            QStringLiteral("x,y,width,height"));
    const QCommandLineOption windowOption(QStringLiteral("window"),
                                          QStringLiteral("Window to record."),
                                          QStringLiteral("identifier"));
    const QCommandLineOption cursorOption(QStringLiteral("cursor"),
                                          QStringLiteral("Include the pointer."));
    const QCommandLineOption microphoneOption(
            QStringLiteral("microphone"),
            QStringLiteral("Record this input, or \"default\"."), QStringLiteral("node"));
    parser.addOptions({outputOption, regionOption, windowOption, cursorOption, microphoneOption});
    parser.addPositionalArgument(QStringLiteral("file"), QStringLiteral("The MP4 file to write."));
    parser.process(app);

    Recorder::Options options;
    options.output = parser.value(outputOption);
    options.window = parser.value(windowOption);
    options.cursor = parser.isSet(cursorOption);
    if (parser.isSet(microphoneOption)) {
        options.microphone = true;
        const QString target = parser.value(microphoneOption);
        if (target != QLatin1String("default"))
            options.microphoneTarget = target;
    }
    if (parser.isSet(regionOption)) {
        const QStringList parts = parser.value(regionOption).split(QLatin1Char(','));
        if (parts.size() == 4)
            options.region = QRect(parts[0].toInt(), parts[1].toInt(), parts[2].toInt(),
                                   parts[3].toInt());
    }
    if (parser.positionalArguments().size() != 1
        || (options.output.isEmpty() == options.window.isEmpty())) {
        fprintf(stderr, "Give one file and either --output or --window.\n");
        return 2;
    }
    options.path = parser.positionalArguments().constFirst();

    Recorder recorder(options);
    QObject::connect(&recorder, &Recorder::started, [] {
        printf("started\n");
        fflush(stdout);
    });
    QObject::connect(&recorder, &Recorder::finished, &app, [&app](bool ok, const QString &error) {
        if (!ok)
            fprintf(stderr, "%s\n", qPrintable(error));
        app.exit(ok ? 0 : 1);
    });

    QString error;
    if (!recorder.start(&error)) {
        fprintf(stderr, "%s\n", qPrintable(error));
        return 1;
    }

    const int signalFd = signalfd(-1, &blocked, SFD_CLOEXEC | SFD_NONBLOCK);
    QSocketNotifier signalNotifier(signalFd, QSocketNotifier::Read);
    QObject::connect(&signalNotifier, &QSocketNotifier::activated, [&] {
        signalfd_siginfo info;
        while (read(signalFd, &info, sizeof(info)) == sizeof(info)) { }
        recorder.stop();
    });

    QSocketNotifier input(STDIN_FILENO, QSocketNotifier::Read);
    QObject::connect(&input, &QSocketNotifier::activated, [&] {
        char buffer[64];
        const ssize_t length = read(STDIN_FILENO, buffer, sizeof(buffer));
        if (length <= 0 || QByteArray(buffer, int(length)).contains("stop")) {
            input.setEnabled(false);
            recorder.stop();
        }
    });

    const int result = app.exec();
    close(signalFd);
    return result;
}
