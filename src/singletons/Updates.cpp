// SPDX-FileCopyrightText: 2018 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "singletons/Updates.hpp"

#include "common/Literals.hpp"
#include "common/Modes.hpp"
#include "common/network/NetworkRequest.hpp"
#include "common/network/NetworkResult.hpp"
#include "common/QLogging.hpp"
#include "common/Version.hpp"
#include "singletons/Paths.hpp"
#include "singletons/Settings.hpp"
#include "util/CombinePath.hpp"
#include "util/PostToThread.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QMessageBox>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStringBuilder>
#include <QtConcurrent>
#include <semver/semver.hpp>

namespace {

using namespace chatterino;
using namespace literals;

QString currentBranch()
{
    return getSettings()->betaUpdates ? "beta" : "stable";
}

#if defined(Q_OS_WIN)
const QString CHATTERINO_OS = u"win"_s;
#elif defined(Q_OS_MACOS)
const QString CHATTERINO_OS = u"macos"_s;
#elif defined(Q_OS_LINUX)
const QString CHATTERINO_OS = u"linux"_s;
#elif defined(Q_OS_FREEBSD)
const QString CHATTERINO_OS = u"freebsd"_s;
#else
const QString CHATTERINO_OS = u"unknown"_s;
#endif

QJsonValue getForArchitecture(const QJsonObject &obj, const QString &key)
{
    auto val = obj[key];

#ifdef Q_PROCESSOR_ARM
    QString armKey = key % u"_arm";
    if (obj[armKey].isString())
    {
        val = obj[armKey];
    }
#elifdef Q_PROCESSOR_X86
    QString x86Key = key % u"_x86";
    if (obj[x86Key].isString())
    {
        val = obj[x86Key];
    }
#endif

    return val;
}

}  // namespace

namespace chatterino {

Updates::Updates(const Modes &modes_, const Paths &paths_, Settings &settings)
    : paths(paths_)
    , modes(modes_)
    , currentVersion_(CHATTERINO_VERSION)
    , updateGuideLink_("https://chatterino.com")
{
    qCDebug(chatterinoUpdate) << "init UpdateManager";

    settings.betaUpdates.connect(
        [this] {
            this->checkForUpdates();
        },
        this->managedConnections, false);
}

/// Checks if the online version is newer or older than the current version.
bool Updates::isDowngradeOf(const QString &online, const QString &current)
{
    semver::version onlineVersion;
    if (!onlineVersion.from_string_noexcept(online.toStdString()))
    {
        qCWarning(chatterinoUpdate) << "Unable to parse online version"
                                    << online << "into a proper semver string";
        return false;
    }

    semver::version currentVersion;
    if (!currentVersion.from_string_noexcept(current.toStdString()))
    {
        qCWarning(chatterinoUpdate) << "Unable to parse current version"
                                    << current << "into a proper semver string";
        return false;
    }

    if (onlineVersion.major == 7)
    {
        onlineVersion.major = 2;
    }

    return onlineVersion < currentVersion;
}

void Updates::deleteOldFiles()
{
    std::ignore = QtConcurrent::run([dir{this->paths.miscDirectory}] {
        {
            auto path = combinePath(dir, "Update.exe");
            if (QFile::exists(path))
            {
                QFile::remove(path);
            }
        }
        {
            auto path = combinePath(dir, "update.zip");
            if (QFile::exists(path))
            {
                QFile::remove(path);
            }
        }
    });
}

const QString &Updates::getCurrentVersion() const
{
    return this->currentVersion_;
}

const QString &Updates::getOnlineVersion() const
{
    return this->onlineVersion_;
}

const QString &Updates::getOnlineCommitHash() const
{
    return this->onlineCommitHash_;
}

const QString &Updates::getOnlineCommitMessage() const
{
    return this->onlineCommitMessage_;
}

void Updates::installUpdates()
{
    if (this->status_ != UpdateAvailable)
    {
        assert(false);
        return;
    }

#if defined(Q_OS_LINUX)
    this->runLinuxUpdater();
    return;
#endif

    if (Version::instance().isNightly())
    {
        // Since Nightly builds can be installed in many different ways, we ask the user to download the update manually.
        QDesktopServices::openUrl(
            QUrl("https://github.com/SevenTV/chatterino7/releases"));
        return;
    }

#ifdef Q_OS_MACOS
    QMessageBox *box = new QMessageBox(
        QMessageBox::Information, "Chatterino Update",
        "A link will open in your browser. Download and install to update.");
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->open();
    QDesktopServices::openUrl(this->updateExe_);
#elif defined Q_OS_WIN
    if (this->modes.isPortable)
    {
        QMessageBox *box =
            new QMessageBox(QMessageBox::Information, "Chatterino Update",
                            "Chatterino is downloading the update "
                            "in the background and will run the "
                            "updater once it is finished.");
        box->setAttribute(Qt::WA_DeleteOnClose);
        box->show();

        NetworkRequest(this->updatePortable_)
            .timeout(600000)
            .followRedirects(true)
            .onError([this](NetworkResult) {
                this->setStatus_(DownloadFailed);

                postToThread([] {
                    QMessageBox *box = new QMessageBox(
                        QMessageBox::Information, "Chatterino Update",
                        "Failed while trying to download the update.");
                    box->setAttribute(Qt::WA_DeleteOnClose);
                    box->show();
                    box->raise();
                });
            })
            .onSuccess([this](auto result) {
                if (result.status() != 200)
                {
                    auto *box = new QMessageBox(
                        QMessageBox::Information, "Chatterino Update",
                        QStringLiteral("The update couldn't be downloaded "
                                       "(Error: %1).")
                            .arg(result.formatError()));
                    box->setAttribute(Qt::WA_DeleteOnClose);
                    box->exec();
                    return;
                }

                QByteArray object = result.getData();
                auto filename =
                    combinePath(this->paths.miscDirectory, "update.zip");

                QFile file(filename);
                if (!file.open(QIODevice::Truncate | QIODevice::WriteOnly))
                {
                    qCWarning(chatterinoUpdate)
                        << "Failed to save update.zip" << file.errorString();
                    this->setStatus_(WriteFileFailed);
                    return;
                }

                if (file.write(object) == -1)
                {
                    this->setStatus_(WriteFileFailed);
                    return;
                }
                file.flush();
                file.close();

                auto updaterPath = Updates::portableUpdaterPath(this->paths);
                if (!QFile::exists(updaterPath))
                {
                    this->setStatus_(MissingPortableUpdater);
                    return;
                }
                bool ok =
                    QProcess::startDetached(updaterPath, {filename, "restart"});
                if (!ok)
                {
                    this->setStatus_(RunUpdaterFailed);
                    return;
                }

                QApplication::exit(0);
            })
            .execute();
        this->setStatus_(Downloading);
    }
    else
    {
        QMessageBox *box =
            new QMessageBox(QMessageBox::Information, "Chatterino Update",
                            "Chatterino is downloading the update "
                            "in the background and will run the "
                            "updater once it is finished.");
        box->setAttribute(Qt::WA_DeleteOnClose);
        box->show();

        NetworkRequest(this->updateExe_)
            .timeout(600000)
            .followRedirects(true)
            .onError([this](NetworkResult) {
                this->setStatus_(DownloadFailed);

                QMessageBox *box = new QMessageBox(
                    QMessageBox::Information, "Chatterino Update",
                    "Failed to download the update. \n\nTry manually "
                    "downloading the update.");
                box->setAttribute(Qt::WA_DeleteOnClose);
                box->exec();
            })
            .onSuccess([this](auto result) {
                if (result.status() != 200)
                {
                    auto *box = new QMessageBox(
                        QMessageBox::Information, "Chatterino Update",
                        QStringLiteral("The update couldn't be downloaded "
                                       "(Error: %1).")
                            .arg(result.formatError()));
                    box->setAttribute(Qt::WA_DeleteOnClose);
                    box->exec();
                    return;
                }

                QByteArray object = result.getData();
                auto filePath =
                    combinePath(this->paths.miscDirectory, "Update.exe");

                QFile file(filePath);
                // write() will fail if we couldn't open
                std::ignore =
                    file.open(QIODevice::Truncate | QIODevice::WriteOnly);

                if (file.write(object) == -1)
                {
                    this->setStatus_(WriteFileFailed);
                    QMessageBox *box = new QMessageBox(
                        QMessageBox::Information, "Chatterino Update",
                        "Failed to save the update file. This could be due to "
                        "window settings or antivirus software.\n\nTry "
                        "manually "
                        "downloading the update.");
                    box->setAttribute(Qt::WA_DeleteOnClose);
                    box->exec();

                    QDesktopServices::openUrl(this->updateExe_);
                    return;
                }
                file.flush();
                file.close();

                if (QProcess::startDetached(filePath, {}))
                {
                    QApplication::exit(0);
                }
                else
                {
                    QMessageBox *box = new QMessageBox(
                        QMessageBox::Information, "Chatterino Update",
                        "Failed to execute update binary. This could be due to "
                        "window "
                        "settings or antivirus software.\n\nTry manually "
                        "downloading "
                        "the update.");
                    box->setAttribute(Qt::WA_DeleteOnClose);
                    box->exec();

                    QDesktopServices::openUrl(this->updateExe_);
                }
            })
            .execute();
        this->setStatus_(Downloading);
    }
#endif
}

void Updates::checkForUpdates()
{
#ifndef CHATTERINO_DISABLE_UPDATER
    auto version = Version::instance();

    if (!version.isSupportedOS())
    {
        qCDebug(chatterinoUpdate)
            << "Update checking disabled because OS doesn't appear to be one "
               "of Windows, GNU/Linux or macOS.";
        return;
    }

    // Disable updates on Flatpak
    if (version.isFlatpak())
    {
        return;
    }

#if defined(Q_OS_LINUX)
    this->setStatus_(Searching);

    QUrl url(
        "https://api.github.com/repos/Tankeeee2/Chatterino-Homies-Linux/commits/main");

    NetworkRequest(url)
        .timeout(30000)
        .header("Accept", "application/vnd.github.v3+json")
        .header("User-Agent", "Chatterino-Homies-Linux")
        .followRedirects(true)
        .onSuccess([this](const NetworkResult &result) {
            const auto object = result.parseJson();
            if (object.isEmpty())
            {
                this->setStatus_(SearchFailed);
                return;
            }

            QString remoteSha = object["sha"].toString();
            if (remoteSha.isEmpty())
            {
                this->setStatus_(SearchFailed);
                return;
            }

            QString commitMsg =
                object["commit"].toObject()["message"].toString();
            QString commitTitle = commitMsg.split('\n').value(0).trimmed();

            QString shortRemoteSha = remoteSha.left(7);
            this->onlineVersion_ = shortRemoteSha;
            this->onlineCommitHash_ = shortRemoteSha;
            this->onlineCommitMessage_ = commitTitle;

            QString localHash = Version::instance().commitHash().trimmed();

            qCDebug(chatterinoUpdate)
                << "Local commit:" << localHash
                << "Remote commit:" << remoteSha;

            if (!localHash.isEmpty() &&
                localHash != "GIT-REPOSITORY-NOT-FOUND" &&
                (remoteSha.startsWith(localHash, Qt::CaseInsensitive) ||
                 localHash.startsWith(shortRemoteSha, Qt::CaseInsensitive)))
            {
                this->setStatus_(NoUpdateAvailable);
            }
            else
            {
                this->setStatus_(UpdateAvailable);
            }
        })
        .onError([this](const NetworkResult &result) {
            qCWarning(chatterinoUpdate)
                << "Failed to check for updates from GitHub:"
                << result.formatError();
            this->setStatus_(SearchFailed);
        })
        .execute();
    return;
#endif

    // See https://github.com/SevenTV/SevenTV/issues/48#issue-2193272289
    // for the proposed structure of the response.
    auto onSuccess = [this](const NetworkResult &result) {
        const auto object = result.parseJson();
        if (object.empty())
        {
            return;  // this should only happen on the v4 url as it's not really mapped
        }

        /// Version available on every platform
        auto version = object["version"];
        if (object["v2_version"_L1].isString())
        {
            version = object["v2_version"_L1].toString();
        }

        if (!version.isString())
        {
            this->setStatus_(SearchFailed);
            qCDebug(chatterinoUpdate)
                << "error checking version - missing 'version'" << object;
            return;
        }

#    if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
        /// Downloads an installer for the new version
        auto updateExeUrl = getForArchitecture(object, u"updateexe"_s);
        if (!updateExeUrl.isString())
        {
            this->setStatus_(SearchFailed);
            qCDebug(chatterinoUpdate)
                << "error checking version - missing 'updateexe'" << object;
            return;
        }

        this->updateExe_ = updateExeUrl.toString();

#        ifdef Q_OS_WIN
        /// Windows portable
        auto portableUrl = getForArchitecture(object, "portable_download");
        if (!portableUrl.isString())
        {
            this->setStatus_(SearchFailed);
            qCDebug(chatterinoUpdate)
                << "error checking version - missing 'portable_download'"
                << object;
            return;
        }
        this->updatePortable_ = portableUrl.toString();
#        endif

#    elif defined(Q_OS_LINUX)
        QJsonValue updateGuide = object.value("updateguide");
        if (updateGuide.isString())
        {
            this->updateGuideLink_ = updateGuide.toString();
        }
#    else
        return;
#    endif

        /// Current version
        this->onlineVersion_ = version.toString();

        /// Update available :)
        // 7TV: Don't treat downgrades as updates.
        if (this->currentVersion_ != this->onlineVersion_ &&
            !Updates::isDowngradeOf(this->onlineVersion_,
                                    this->currentVersion_))
        {
            this->setStatus_(UpdateAvailable);
        }
        else
        {
            this->setStatus_(NoUpdateAvailable);
        }
    };

    // We're trying v3, ~~and v4~~ to get updates.
    // The first successful one will be used
    auto apiVersion = std::make_shared<uint8_t>(3);
    constexpr auto maxApiVersion =
        3;  // don't try v4 yet (we don't know the API scheme yet)
    auto fmtUrl = [apiVersion]() -> QString {
        return u"https://7tv.io/v" % QString::number(*apiVersion) %
               "/chatterino/version/" % CHATTERINO_OS % "/" % currentBranch();
    };

    auto onError = std::make_shared<std::function<void(NetworkResult)>>();
    // We need to avoid cyclic ownership, so we pass onError as a weak pointer.
    // During the request, it's kept alive by the finally handler, which will
    // always be called after onError and onSuccess.
    auto makeRequest = [onSuccess,
                        onErrorWeak = std::weak_ptr(onError)](auto url) {
        auto onError = onErrorWeak.lock();
        if (!onError)
        {
            return;
        }
        qCDebug(chatterinoUpdate) << "Requesting updates from" << url;
        NetworkRequest(url)
            .timeout(60000)
            .followRedirects(true)
            .onSuccess(onSuccess)
            .onError(*onError)
            .finally([onError]() {})
            .execute();
    };

    *onError = [apiVersion, fmtUrl, makeRequest](const auto &) mutable {
        if (*apiVersion >= maxApiVersion)
        {
            return;  // nothing returned a response, we're done
        }
        (*apiVersion)++;
        makeRequest(fmtUrl());
    };
    makeRequest(fmtUrl());

    this->setStatus_(Searching);
#endif
}

Updates::Status Updates::getStatus() const
{
    return this->status_;
}

QString Updates::portableUpdaterPath(const Paths &paths)
{
    return combinePath(paths.rootAppDataDirectory,
                       "updater.1/ChatterinoUpdater.exe");
}

bool Updates::shouldShowUpdateButton() const
{
    switch (this->getStatus())
    {
        case UpdateAvailable:
        case SearchFailed:
        case Downloading:
        case DownloadFailed:
        case WriteFileFailed:
            return true;

        default:
            return false;
    }
}

bool Updates::isError() const
{
    switch (this->getStatus())
    {
        case SearchFailed:
        case DownloadFailed:
        case WriteFileFailed:
        case MissingPortableUpdater:
        case RunUpdaterFailed:
            return true;

        default:
            return false;
    }
}

bool Updates::isDowngrade() const
{
    return this->isDowngrade_;
}

QString Updates::buildUpdateAvailableText() const
{
#if defined(Q_OS_LINUX)
    if (!this->onlineCommitHash_.isEmpty())
    {
        return QString(
                   "A new update for Chatterino Homies is available!\n\n"
                   "Commit: %1\n"
                   "%2\n\n"
                   "Click 'Install' to open a terminal, update the repository, "
                   "and recompile/install.")
            .arg(this->onlineCommitHash_, this->onlineCommitMessage_);
    }
#endif

    const auto &version = Version::instance();

    if (version.isNightly())
    {
        // Since Nightly builds can be installed in many different ways, we ask the user to download the update manually.
        if (this->isDowngrade())
        {
            return QString(
                       "The version online (%1) seems to be lower than the "
                       "current (%2).\nEither a version was reverted or "
                       "you are running a newer build.\n\nDo you want to "
                       "head to github.com/SevenTV/chatterino7 to download it?")
                .arg(this->getOnlineVersion(), this->getCurrentVersion());
        }

        return QString(
                   "An update (%1) is available.\n\nDo you want to head to "
                   "github.com/SevenTV/chatterino7 to download the new update?")
            .arg(this->getOnlineVersion());
    }

    if (this->isDowngrade())
    {
        return QString("The version online (%1) seems to be lower than the "
                       "current (%2).\nEither a version was reverted or "
                       "you are running a newer build.\n\nDo you want to "
                       "download and install it?")
            .arg(this->getOnlineVersion(), this->getCurrentVersion());
    }

    return QString("An update (%1) is available.\n\nDo you want to "
                   "download and install it?")
        .arg(this->getOnlineVersion());
}

void Updates::setStatus_(Status status)
{
    if (this->status_ != status)
    {
        this->status_ = status;
        postToThread([this, status] {
            this->statusUpdated.invoke(status);
        });
    }
}

#if defined(Q_OS_LINUX)
void Updates::runLinuxUpdater()
{
    // 1. Localizar el script de actualización
    QString scriptPath;

    // A. Comprobar en el repo fuente si estamos ejecutando en desarrollo
    QString appDir = QCoreApplication::applicationDirPath();
    QDir dir(appDir);
    // Si estamos en build/bin, el repo está dos niveles arriba
    if (dir.cdUp() && dir.cdUp())
    {
        QString candidate = dir.filePath("scripts/update-chatterino.sh");
        if (QFile::exists(candidate))
        {
            scriptPath = candidate;
        }
    }

    // B. Comprobar en rutas instaladas del sistema
    if (scriptPath.isEmpty())
    {
        QString installedBin =
            QStandardPaths::findExecutable("chatterino-update");
        if (!installedBin.isEmpty())
        {
            scriptPath = installedBin;
        }
    }

    if (scriptPath.isEmpty())
    {
        QString sysScript = "/usr/share/chatterino/scripts/update-chatterino.sh";
        if (QFile::exists(sysScript))
        {
            scriptPath = sysScript;
        }
    }

    // C. Fallback: extraer script temporal ejecutable si no se encuentra en disco
    if (scriptPath.isEmpty())
    {
        QString tempScript =
            combinePath(this->paths.miscDirectory, "update-chatterino.sh");
        QFile file(tempScript);
        if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        {
            const char embeddedScript[] =
                "#!/usr/bin/env bash\n"
                "set -e\n"
                "REPO_DIR=\"$1\"\n"
                "if [[ -z \"$REPO_DIR\" ]]; then\n"
                "    for c in \"$HOME/chatterino7\" \"$HOME/Chatterino-Homies-Linux\" \"/home/juanjo/chatterino7\"; do\n"
                "        if [[ -d \"$c/.git\" ]]; then REPO_DIR=\"$c\"; break; fi\n"
                "    done\n"
                "fi\n"
                "if [[ -z \"$REPO_DIR\" ]]; then\n"
                "    REPO_DIR=\"$HOME/.local/src/chatterino7\"\n"
                "    mkdir -p \"$(dirname \"$REPO_DIR\")\"\n"
                "    git clone --recurse-submodules https://github.com/Tankeeee2/Chatterino-Homies-Linux.git \"$REPO_DIR\"\n"
                "fi\n"
                "cd \"$REPO_DIR\"\n"
                "echo \"== Chatterino Homies Updater ==\"\n"
                "echo \">> Updating repository...\"\n"
                "git pull origin main\n"
                "git submodule sync --recursive\n"
                "git submodule update --init --recursive\n"
                "echo \">> Building...\"\n"
                "cmake -B build -DCMAKE_INSTALL_PREFIX=/usr -DBUILD_TESTS=OFF -DBUILD_BENCHMARKS=OFF -DUSE_PRECOMPILED_HEADERS=OFF -DCHATTERINO_LTO=OFF -DCHATTERINO_UPDATER=ON -DCMAKE_BUILD_TYPE=Release\n"
                "cmake --build build -j$(nproc)\n"
                "echo \">> Installing (root password required):\"\n"
                "sudo cmake --install build\n"
                "echo \"Update complete! Restarting Chatterino...\"\n"
                "pkill -x chatterino || pkill -x chatterino-homies || true\n"
                "nohup /usr/bin/chatterino >/dev/null 2>&1 &\n";

            file.write(embeddedScript);
            file.flush();
            file.close();
            file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                QFileDevice::ExeOwner | QFileDevice::ReadGroup |
                                QFileDevice::ExeGroup | QFileDevice::ReadOther |
                                QFileDevice::ExeOther);
            scriptPath = tempScript;
        }
    }

    // 2. Determinar el directorio del repo preferente
    QString repoDir;
    QDir devRepo(appDir);
    if (devRepo.cdUp() && devRepo.cdUp() && devRepo.exists(".git"))
    {
        repoDir = devRepo.canonicalPath();
    }

    // 3. Lanzar la terminal
    // Prioridad 1: Omarchy floating terminal launcher si existe
    QString omarchyLauncher = QStandardPaths::findExecutable(
        "omarchy-launch-floating-terminal-with-presentation");
    if (!omarchyLauncher.isEmpty())
    {
        QString command = QString("\"%1\"").arg(scriptPath);
        if (!repoDir.isEmpty())
        {
            command += QString(" \"%1\"").arg(repoDir);
        }
        if (QProcess::startDetached(omarchyLauncher, {command}))
        {
            return;
        }
    }

    // Prioridad 2: Terminales estándar
    struct TermDef {
        QString binary;
        QStringList args;
    };

    std::vector<TermDef> terminals;
    QString envTerm = qEnvironmentVariable("TERMINAL");
    if (!envTerm.isEmpty())
    {
        terminals.push_back({envTerm, {"-e"}});
    }
    terminals.push_back(
        {"xdg-terminal-exec", {"--title=Actualizando Chatterino Homies", "-e"}});
    terminals.push_back(
        {"ghostty", {"--title=Actualizando Chatterino Homies", "-e"}});
    terminals.push_back({"kitty", {"--title=Actualizando Chatterino Homies"}});
    terminals.push_back(
        {"alacritty", {"--title", "Actualizando Chatterino Homies", "-e"}});
    terminals.push_back({"foot", {"--title=Actualizando Chatterino Homies"}});
    terminals.push_back(
        {"konsole", {"--title", "Actualizando Chatterino Homies", "-e"}});
    terminals.push_back(
        {"xterm", {"-title", "Actualizando Chatterino Homies", "-e"}});

    for (const auto &term : terminals)
    {
        QString termExe = QStandardPaths::findExecutable(term.binary);
        if (!termExe.isEmpty())
        {
            QStringList fullArgs = term.args;
            fullArgs << scriptPath;
            if (!repoDir.isEmpty())
            {
                fullArgs << repoDir;
            }
            if (QProcess::startDetached(termExe, fullArgs))
            {
                return;
            }
        }
    }

    QMessageBox *box = new QMessageBox(
        QMessageBox::Warning, "Chatterino Update",
        "Could not detect a terminal emulator to run the updater.\n"
        "Please run 'chatterino-update' or 'git pull && makepkg -si' manually.");
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->open();
}
#endif

}  // namespace chatterino
