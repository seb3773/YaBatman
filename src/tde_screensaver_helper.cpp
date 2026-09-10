#include "tde_screensaver_helper.h"
#include <tqdir.h>
#include <tqfile.h>
#include <tqtextstream.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>

static TQString getSystemLanguagePrefix() {
    const char *lang = getenv("LC_ALL");
    if (!lang || !*lang) lang = getenv("LC_MESSAGES");
    if (!lang || !*lang) lang = getenv("LANG");
    if (!lang || !*lang) return "fr";
    TQString s(lang);
    int idx = s.find('_');
    if (idx > 0) return s.left(idx);
    idx = s.find('.');
    if (idx > 0) return s.left(idx);
    return s;
}

TQString TDEScreensavers::findBinaryPath(const TQString &execName) {
    if (execName.isEmpty()) return "";

    TQString cleanExec = execName;
    int spaceIdx = cleanExec.find(' ');
    if (spaceIdx > 0) {
        cleanExec = cleanExec.left(spaceIdx);
    }

    // Direct path check
    if (cleanExec.startsWith("/") && access(cleanExec.latin1(), X_OK) == 0) {
        return cleanExec;
    }

    // Check standard Trinity locations
    TQStringList searchDirs;
    searchDirs.append("/opt/trinity/bin/");
    searchDirs.append("/usr/bin/");
    searchDirs.append("/usr/local/bin/");

    // Also check PATH environment variable
    const char *pathEnv = getenv("PATH");
    if (pathEnv && *pathEnv) {
        TQString pStr(pathEnv);
        TQStringList envDirs = TQStringList::split(':', pStr);
        for (TQStringList::Iterator eit = envDirs.begin(); eit != envDirs.end(); ++eit) {
            TQString d = *eit;
            if (!d.endsWith("/")) d += "/";
            if (!searchDirs.contains(d)) searchDirs.append(d);
        }
    }

    for (TQStringList::Iterator it = searchDirs.begin(); it != searchDirs.end(); ++it) {
        TQString candidate = (*it) + cleanExec;
        if (access(candidate.latin1(), X_OK) == 0) {
            return candidate;
        }
    }

    return "";
}

bool TDEScreensavers::parseDesktopFile(const TQString &filePath, TDEScreensaverInfo &info) {
    TQFile file(filePath);
    if (!file.open(IO_ReadOnly)) return false;

    TQTextStream ts(&file);
    TQString line;
    TQString langPrefix = getSystemLanguagePrefix();
    TQString localizedNameKey = TQString("Name[") + langPrefix + "]=";

    TQString defaultName = "";
    TQString localizedName = "";
    TQString execStr = "";
    TQString category = "";
    bool inSetupAction = false;
    bool hasSetup = false;
    bool inDesktopEntry = false;

    while (!ts.atEnd()) {
        line = ts.readLine().stripWhiteSpace();
        if (line.isEmpty() || line.startsWith("#")) continue;

        if (line.startsWith("[") && line.endsWith("]")) {
            if (line == "[Desktop Entry]") {
                inDesktopEntry = true;
                inSetupAction = false;
            } else if (line == "[Desktop Action Setup]") {
                inDesktopEntry = false;
                inSetupAction = true;
                hasSetup = true;
            } else {
                inDesktopEntry = false;
                inSetupAction = false;
            }
            continue;
        }

        if (inDesktopEntry) {
            if (line.startsWith("Exec=")) {
                execStr = line.mid(5).stripWhiteSpace();
            } else if (line.startsWith(localizedNameKey)) {
                localizedName = line.mid(localizedNameKey.length()).stripWhiteSpace();
            } else if (line.startsWith("Name=") && defaultName.isEmpty()) {
                defaultName = line.mid(5).stripWhiteSpace();
            } else if (line.startsWith("X-TDE-Category=")) {
                category = line.mid(15).stripWhiteSpace();
            } else if (line.startsWith("Actions=")) {
                if (line.contains("Setup")) {
                    hasSetup = true;
                }
            }
        }
    }
    file.close();

    if (execStr.isEmpty()) return false;

    // Clean exec token
    TQString binaryOnly = execStr;
    int sp = binaryOnly.find(' ');
    if (sp > 0) binaryOnly = binaryOnly.left(sp);

    TQString fullPath = findBinaryPath(binaryOnly);
    if (fullPath.isEmpty()) {
        // Executable not found or not runnable on this system
        return false;
    }

    info.execName = binaryOnly;
    info.fullPath = fullPath;
    info.id = TQString("tde:") + binaryOnly;
    info.name = !localizedName.isEmpty() ? localizedName : (!defaultName.isEmpty() ? defaultName : binaryOnly);
    info.hasSetup = hasSetup;
    info.category = category;

    return true;
}

static int compareSavers(const TDEScreensaverInfo &a, const TDEScreensaverInfo &b) {
    return a.name.localeAwareCompare(b.name) < 0;
}

TQValueList<TDEScreensaverInfo> TDEScreensavers::getAvailableScreensavers() {
    TQValueList<TDEScreensaverInfo> list;
    TQStringList scannedExecs;

    TQStringList searchDirs;
    searchDirs.append("/opt/trinity/share/applnk/System/ScreenSavers");
    searchDirs.append("/usr/share/applnk/System/ScreenSavers");

    for (TQStringList::Iterator dit = searchDirs.begin(); dit != searchDirs.end(); ++dit) {
        TQDir dir(*dit);
        if (!dir.exists()) continue;

        TQStringList entries = dir.entryList("*.desktop", TQDir::Files);
        for (TQStringList::Iterator fit = entries.begin(); fit != entries.end(); ++fit) {
            TQString fullPath = dir.filePath(*fit);
            TDEScreensaverInfo info;
            if (parseDesktopFile(fullPath, info)) {
                if (!scannedExecs.contains(info.execName)) {
                    scannedExecs.append(info.execName);
                    list.append(info);
                }
            }
        }
    }

    // Sort alphabetically by localized name
    qHeapSort(list);

    return list;
}

bool TDEScreensavers::launchSetup(const TQString &idOrExec) {
    TQString clean = idOrExec;
    if (clean.startsWith("tde:")) {
        clean = clean.mid(4);
    }
    TQString path = findBinaryPath(clean);
    if (path.isEmpty()) return false;

    // Double-fork to avoid zombie process
    pid_t pid = fork();
    if (pid < 0) return false;
    if (pid == 0) {
        // First child
        pid_t p2 = fork();
        if (p2 == 0) {
            // Grandchild: runs setup
            execl(path.latin1(), path.latin1(), "-setup", NULL);
            _exit(1);
        }
        _exit(0);
    }
    // Parent waits for immediate first child
    waitpid(pid, NULL, 0);
    return true;
}
