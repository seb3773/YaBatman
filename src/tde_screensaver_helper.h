#ifndef TDE_SCREENSAVER_HELPER_H
#define TDE_SCREENSAVER_HELPER_H

#include <tqstring.h>
#include <tqstringlist.h>
#include <tqvaluelist.h>

struct TDEScreensaverInfo {
    TQString id;        // Config identifier, e.g. "tde:kspace.kss"
    TQString name;      // Display name, e.g. "Space (GL)"
    TQString execName;  // Binary name, e.g. "kspace.kss"
    TQString fullPath;  // Full executable path, e.g. "/opt/trinity/bin/kspace.kss"
    bool hasSetup;      // True if setup/configuration action exists
    TQString category;  // Category, e.g. "OpenGL"

    bool operator<(const TDEScreensaverInfo &other) const {
        return name.localeAwareCompare(other.name) < 0;
    }
};

class TDEScreensavers {
public:
    // Discover and return all installed and executable Trinity screensavers
    static TQValueList<TDEScreensaverInfo> getAvailableScreensavers();

    // Find full binary path for an executable name (checks /opt/trinity/bin then PATH)
    static TQString findBinaryPath(const TQString &execName);

    // Launch configuration dialog for a screensaver (-setup)
    static bool launchSetup(const TQString &idOrExec);

private:
    static bool parseDesktopFile(const TQString &filePath, TDEScreensaverInfo &info);
};

#endif // TDE_SCREENSAVER_HELPER_H
