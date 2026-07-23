#ifndef YABATMAN_UTILS_H
#define YABATMAN_UTILS_H

#include <tqstring.h>
#include <tqfile.h>
#include <tqtextstream.h>
#include <tqslider.h>
#include <tqevent.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <dirent.h>
#include <unistd.h>
#include <linux/wireless.h>

class ClickJumpSlider : public TQSlider {
public:
    ClickJumpSlider(TQWidget *parent, const char* name = 0)
        : TQSlider(parent, name) {}
    ClickJumpSlider(Orientation orient, TQWidget *parent, const char* name = 0)
        : TQSlider(orient, parent, name) {}
    ClickJumpSlider(int minValue, int maxValue, int pageStep, int value, Orientation orient,
                    TQWidget *parent, const char* name = 0)
        : TQSlider(minValue, maxValue, pageStep, value, orient, parent, name) {}

protected:
    virtual void mousePressEvent(TQMouseEvent *e) {
        if (e->button() == TQt::LeftButton && !sliderRect().contains(e->pos())) {
            int val = 0;
            if (orientation() == TQt::Horizontal) {
                int handleWidth = sliderRect().width();
                int available = width() - handleWidth;
                if (available > 0) {
                    int hx = e->pos().x() - handleWidth / 2;
                    if (hx < 0) hx = 0;
                    if (hx > available) hx = available;
                    double ratio = (double)hx / available;
                    val = minValue() + (int)(ratio * (maxValue() - minValue()) + 0.5);
                } else {
                    val = minValue();
                }
            } else {
                int handleHeight = sliderRect().height();
                int available = height() - handleHeight;
                if (available > 0) {
                    int hy = e->pos().y() - handleHeight / 2;
                    if (hy < 0) hy = 0;
                    if (hy > available) hy = available;
                    double ratio = 1.0 - ((double)hy / available);
                    val = minValue() + (int)(ratio * (maxValue() - minValue()) + 0.5);
                } else {
                    val = minValue();
                }
            }
            setValue(val);
        }
        TQSlider::mousePressEvent(e);
    }
};

inline int readSysfsInt(const TQString& path) {
    TQFile file(path);
    if (file.open(IO_ReadOnly)) {
        TQTextStream stream(&file);
        TQString val = stream.readLine().stripWhiteSpace();
        return val.toInt();
    }
    return 0;
}

inline TQString readSysfsString(const TQString& path) {
    TQFile file(path);
    if (file.open(IO_ReadOnly)) {
        TQTextStream stream(&file);
        return stream.readLine().stripWhiteSpace();
    }
    return "";
}

inline void callDaemonSocket(const TQString& cmd) {
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) return;

    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, "/run/yabatmand/daemon.sock", sizeof(addr.sun_path) - 1);

    if (::connect(sock, (struct sockaddr*)&addr, sizeof(addr)) >= 0) {
        write(sock, cmd.latin1(), cmd.length());
        write(sock, "\n", 1);
    }
    ::close(sock);
}

inline TQString getWifiInterfaceName() {
    DIR *dir = opendir("/sys/class/net");
    if (!dir) return "";
    struct dirent *entry;
    TQString interfaceName = "";
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd == -1) {
        closedir(dir);
        return "";
    }
    while ((entry = readdir(dir))) {
        if (entry->d_name[0] == '.') continue;
        struct iwreq req;
        memset(&req, 0, sizeof(struct iwreq));
        strncpy(req.ifr_name, entry->d_name, IFNAMSIZ - 1);
        if (ioctl(sockfd, SIOCGIWNAME, &req) >= 0) {
            interfaceName = entry->d_name;
            break;
        }
    }
    close(sockfd);
    closedir(dir);
    return interfaceName;
}

inline TQString getActiveSsid() {
    TQString ifName = getWifiInterfaceName();
    if (ifName.isEmpty()) return "";

    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd == -1) return "";

    struct iwreq req;
    memset(&req, 0, sizeof(struct iwreq));
    strncpy(req.ifr_name, ifName.latin1(), IFNAMSIZ - 1);

    char ssid[IW_ESSID_MAX_SIZE + 1];
    memset(ssid, 0, sizeof(ssid));
    req.u.essid.pointer = ssid;
    req.u.essid.length = IW_ESSID_MAX_SIZE;
    req.u.essid.flags = 0;

    if (ioctl(sockfd, SIOCGIWESSID, &req) == -1) {
        close(sockfd);
        return "";
    }
    close(sockfd);
    return TQString(ssid);
}

#endif // YABATMAN_UTILS_H
