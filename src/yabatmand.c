
// -----------------------------------------------------------------------------------------------
//  [-= YaBatmanD  ~~ Yet Another Battery Manager Daemon =-]
//
// > yabatman daemon for controlling cpu freq/power management, governor,
//      usb autosuspend, sata link power, io scheduler etc.. 
// > Need to be run as root. 
// -----------------------------------------------------------------------------------------------


//detect if needed tools exists, if not, send notif (via system call) (and exit ?....) (or run anyway with less capabilities ?)
//retrieve schedulers at start, like governors (adapter fonction)
//retrieve some other paths at start ?..


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <errno.h>
#include <glib.h>
#include <glob.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <linux/if_arp.h>
#include <stdint.h>
#include <limits.h>
#include <systemd/sd-bus.h>
#include <regex.h>
#include <linux/ethtool.h>
#include <linux/sockios.h>

#define SOCK_PATH "/run/yabatmand/daemon.sock"
#define CPU_SYSFS_PATH "/sys/devices/system/cpu/"
#define MAX_CPUS 128
#define MAX_FREEZABLE         100
#define SAMPLE_INTERVAL_SEC   1
#define CPU_PCT_THRESHOLD     1.0f     // % cpu max on interval
#define MEM_THRESHOLD_KB      20000UL  // (20 Mo)
#define MAX_ENTRIES 64
#define MAX_ENTRY_LEN 64
#define CMD_BUFSZ 4096

#define PATTERN_MSG "(discord|slack|teams|zoom|skype|mattermost|element|thunderbird)"
#define PATTERN_SYNC "(nextcloud|dropbox|syncthing|megasync|seaf|insync|onedrive|unattended-upgrade)"


#define my_isdigit(c) ((c) >= '0' && (c) <= '9')
#define em_write_sysfs(path_str, val_str) (({ FILE *f = fopen((path_str), "w"); int r = -1; if (f) { if (fputs(val_str, f) >= 0) r = 0; fclose(f); } r; }))

#ifdef CONSOLE_DEBUG
static inline const char* debugtag(void) {
static char buffer[16];
time_t now = time(NULL);
struct tm* tm_info = localtime(&now);
snprintf(buffer, sizeof(buffer), "#%02d:%02d:%02d|", tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec);
return buffer; }
#endif

volatile sig_atomic_t keep_running = 1;

static regex_t rx_msg_cached, rx_sync_cached;
static int regex_compiled = 0;

static gboolean reboot_or_poweroff=FALSE;
static gboolean frozen_procs = FALSE;

static int cpu_online_initial[MAX_CPUS];
static int cpu_count = 0;
static int whitelist_size = 0;
static int blacklist_size = 0;
static int procs_whitelist_size = 0;
static int procs_blacklist_size = 0;

int whitelist_count = 0;
int blacklist_count = 0;
int freezable_count = 0;
int frozen_count = 0;
int merged_whitelist_count = 0;
int procs_whitelist_count = 0;
int procs_blacklist_count = 0;

char procs_whitelist[MAX_ENTRIES][MAX_ENTRY_LEN];
char procs_blacklist[MAX_ENTRIES][MAX_ENTRY_LEN];
char iface_name[IFNAMSIZ] = {0};
char whitelist[MAX_ENTRIES][MAX_ENTRY_LEN];
char blacklist[MAX_ENTRIES][MAX_ENTRY_LEN];
char freezable_services[MAX_FREEZABLE][64];
char frozen_services[MAX_FREEZABLE][64];
char merged_whitelist[MAX_FREEZABLE][64];

static char global_buffer_256[256];
static char *webcam_path = NULL;
static char available_governors[256];

static unsigned long max_freq_khz_sys;
static unsigned long min_freq_khz_sys;

// Advanced profile tuning (configurable from GUI)
static int eco_freq_cap_pct = 40;
static int balanced_usb_autosuspend_flag = 1;


typedef enum {
    CPU_TYPE_UNKNOWN = 0,
    CPU_TYPE_INTEL,
    CPU_TYPE_AMD
} cpu_type_t;

static cpu_type_t cpu_type;

typedef enum {
    PROFILE_LOW_POWER,
    PROFILE_ULTRA_LOW_POWER,
    PROFILE_BALANCED,
    PROFILE_PERFORMANCE
} profile_type_t;

const char *platform_profile_paths[] = {
    // standard
    "/sys/firmware/acpi/platform_profile",
    // lenovo thinkpad
    "/sys/bus/platform/drivers/thinkpad_acpi/platform_profile",
    "/sys/devices/platform/thinkpad_acpi/platform_profile",
    // dell
    "/sys/class/hwmon/hwmon0/platform_profile",
    "/sys/class/hwmon/hwmon1/platform_profile",
    "/sys/class/hwmon/hwmon2/platform_profile",
    "/sys/devices/platform/dell-laptop/platform_profile",
    // HP
    "/sys/devices/platform/hp-wmi/platform_profile",
    "/sys/class/hwmon/hwmon0/device/platform_profile",
    // ASUS
    "/sys/devices/platform/asus-nb-wmi/platform_profile",
    "/sys/class/hwmon/hwmon0/device/platform_profile",
    // Generic
    "/sys/devices/platform/platform_profile",
    "/sys/class/platform_profile/platform_profile0/platform_profile",
    NULL
};

static const char *const dirty_params_paths[] = {
    "/proc/sys/vm/dirty_writeback_centisecs",
    "/proc/sys/vm/dirty_expire_centisecs",
    "/proc/sys/fs/xfs/age_buffer_centisecs",
    "/proc/sys/fs/xfs/xfssyncd_centisecs"
};
static const char *const xfsbufd_path = "/proc/sys/fs/xfs/xfsbufd_centisecs";

typedef struct {
    char name[128];
    uint32_t pid;
    float last_cpu;
} service_sample_t;
service_sample_t samples[MAX_FREEZABLE];
int sample_count = 0;

// === hardcoded whitelist  (do not freeze these services)
const char *hardcoded_whitelist[] = {
    "dbus-daemon", "dbus","NetworkManager", "systemd-journald", "systemd-logind",
    "systemd-udevd", "polkitd", "polkit", "accounts-daemon", "gdm", "lightdm", "tdm",
    "sshd", "wpa_supplicant", "upowerd", "udisksd", "gvfsd",  "acpid", "user@1000", "upower",
    "firewalld", "rsyslogd",  "pipewire", "pulseaudio", "winbind",
    "rtkit-daemon", "systemd-networkd", "udisks2",
    "getty@tty1", "rpcbind", "rpc-statd", "nfs-mountd", "nfsdcld", "nfs-blkmap",  "nfs-idmapd",
    "yabatmand"
};
const int hardcoded_whitelist_size = sizeof(hardcoded_whitelist) / sizeof(hardcoded_whitelist[0]);




static int em_set_webcam_power(int enable);
static void add_blacklist_to_freezables();
static void scan_service_activity();
static void merge_whitelists();






void parse_list(const char *input, char list[][MAX_ENTRY_LEN], int *count) {
    char buf[CMD_BUFSZ];
    strncpy(buf, input, sizeof(buf));
    buf[sizeof(buf) - 1] = '\0';
    *count = 0;
    char *buffer_ptr = buf;
    char *token;
    while ((token = strsep(&buffer_ptr, ",")) != NULL && *count < MAX_ENTRIES) {
        if (*token == '\0') continue;
        strncpy(list[*count], token, MAX_ENTRY_LEN - 1);
        list[*count][MAX_ENTRY_LEN - 1] = '\0';
        (*count)++;
    }
}



void set_whitelist_from_string(const char *arg) {
    parse_list(arg, whitelist, &whitelist_count);
    #ifdef CONSOLE_DEBUG
    printf("\033[37m%s \033[38;2;255;255;0m Received whitelist (services to never freeze) (%d entries): \033[39m\n",debugtag(),whitelist_count);
    for (int i = 0; i < whitelist_count; i++) {
    printf("\033[37m%s \033[38;2;255;255;0m    --> %s\033[39m\n",debugtag(), whitelist[i]);
    }
    #endif
    whitelist_size = whitelist_count;
    merge_whitelists();
}

void set_blacklist_from_string(const char *arg) {
    parse_list(arg, blacklist, &blacklist_count);
        #ifdef CONSOLE_DEBUG
    printf("\033[37m%s \033[38;2;255;255;0m Received blacklist (services to always freeze) (%d entries): \033[39m\n",debugtag(),blacklist_count);
    for (int i = 0; i < blacklist_count; i++) {
    printf("\033[37m%s \033[38;2;255;255;0m    --> %s\033[39m\n",debugtag(), blacklist[i]);
    }
    #endif
    blacklist_size = blacklist_count;
}

void set_procs_whitelist_from_string(const char *arg) {
    parse_list(arg, procs_whitelist, &procs_whitelist_count);
    #ifdef CONSOLE_DEBUG
    printf("\033[37m%s \033[38;2;255;255;0m Received processes whitelist (processes to never freeze) (%d entries): \033[39m\n", debugtag(), procs_whitelist_count);
    for (int i = 0; i < procs_whitelist_count; i++) {
        printf("\033[37m%s \033[38;2;255;255;0m    --> %s\033[39m\n", debugtag(), procs_whitelist[i]);
    }
    #endif
    procs_whitelist_size = procs_whitelist_count;
}

void set_procs_blacklist_from_string(const char *arg) {
    parse_list(arg, procs_blacklist, &procs_blacklist_count);
    #ifdef CONSOLE_DEBUG
    printf("\033[37m%s \033[38;2;255;255;0m Received processes blacklist (processes to always freeze) (%d entries): \033[39m\n", debugtag(), procs_blacklist_count);
    for (int i = 0; i < procs_blacklist_count; i++) {
        printf("\033[37m%s \033[38;2;255;255;0m    --> %s\033[39m\n", debugtag(), procs_blacklist[i]);
    }
    #endif
    procs_blacklist_size = procs_blacklist_count;
}

// Fonction utilitaire pour vérifier si un processus est dans la whitelist
int is_proc_whitelisted(const char *proc_cmd) {
    for (int i = 0; i < procs_whitelist_count; i++)
        if (strstr(proc_cmd, procs_whitelist[i]) != NULL)
            return 1;
    return 0;
}

// Fonction utilitaire pour vérifier si un processus est dans la blacklist
int is_proc_blacklisted(const char *proc_cmd) {
    for (int i = 0; i < procs_blacklist_count; i++)
        if (strstr(proc_cmd, procs_blacklist[i]) != NULL)
            return 1;
    return 0;
}




static inline int em_read_sysfs(const char *p, char *b, size_t s) {
    int fd = open(p, O_RDONLY);
    if (fd < 0) return -1;
    ssize_t r = read(fd, b, s - 1);
    close(fd);
    if (r <= 0) return -1;
    while (r && (b[r-1] == '\n' || b[r-1] == '\r')) --r;
    b[r] = 0;
    return 0;
}


static inline int em_read_sysfs_ul(const char *p, unsigned long *v) {
    char b[64]; int fd = open(p, O_RDONLY);
    if (fd < 0) return -1;
    ssize_t r = read(fd, b, 63);
    close(fd);
    if (r <= 0) return -1;
    while (r && (b[r-1] == '\n' || b[r-1] == '\r')) --r;
    b[r] = 0;
    *v = strtoul(b, 0, 10);
    return 0;
}



static inline int str_contains_insensitive(const char *haystack, const char *needle) {
    char haystack_lower[256];
    char needle_lower[64];
    size_t i;
    for (i = 0; i < sizeof(haystack_lower) - 1 && haystack[i]; i++) {
        char c = haystack[i];
        haystack_lower[i] = (c >= 'A' && c <= 'Z') ? (c + 32) : c;
    }
    haystack_lower[i] = '\0';
    for (i = 0; i < sizeof(needle_lower) - 1 && needle[i]; i++) {
        char c = needle[i];
        needle_lower[i] = (c >= 'A' && c <= 'Z') ? (c + 32) : c;
    }
    needle_lower[i] = '\0';
    return strstr(haystack_lower, needle_lower) != NULL;
}




static int is_digit_str(const char *s) {
    if (!s || !*s) return 0;
    for (; *s; ++s) {
        if (!my_isdigit(*s)) return 0;
    }
    return 1;
}


static int read_cmdline(const char *pid, char *buf, size_t sz) {
    char path[32] = "/proc/";
    int i = 6;
    while (pid[i - 6] && i < 31) {
        path[i] = pid[i - 6];
        i++;
    }
    char *suffix = "/cmdline";
    for (int j = 0; suffix[j]; ++j)
        path[i++] = suffix[j];
    path[i] = 0;
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    ssize_t n = read(fd, buf, sz - 1);
    close(fd);
    if (n <= 0) return -1;
    for (ssize_t k = 0; k < n; k++) {
        if (buf[k] == '\0') buf[k] = ' ';
    }
    buf[n] = '\0';
    return 0;
}




unsigned freeze_processes(int freeze) {
    if (freeze == 0) {
        if (!frozen_procs) {
        #ifdef CONSOLE_DEBUG
        printf("\033[37m%s \033[38;2;255;127;127m ¤¤ process_manager: \033[38;2;255;255;0mNo frozen processes to unfreeze\033[39m\n", debugtag());
        #endif
        return 0;
                           } else { frozen_procs = FALSE; }
    }
    if (freeze == 1) {
            if (frozen_procs) {   
            #ifdef CONSOLE_DEBUG
            printf("\033[37m%s \033[38;2;255;127;127m ¤¤ process_manager:\033[38;2;255;105;97mProcesses already freezed.\033[39m\n",debugtag());
            #endif
            return 0; } else { frozen_procs = TRUE; }
    }
    
    if (!regex_compiled) return 0;
    
    DIR *dp = opendir("/proc");
    if (!dp) {
        perror("opendir /proc");
        return 0;
    }
    
    struct dirent *de;
    char cmd[CMD_BUFSZ];
    unsigned count = 0;
    pid_t self_pid = getpid();
    
    while ((de = readdir(dp)) != NULL) {
        if (!is_digit_str(de->d_name)) continue;
        if (read_cmdline(de->d_name, cmd, sizeof cmd) != 0) continue;
        
        pid_t pid = (pid_t)strtol(de->d_name, NULL, 10);
        if (pid == self_pid) continue;
        
        int should_freeze = 0;
        const char *reason = "";
        
        if (is_proc_blacklisted(cmd)) {
            should_freeze = 1;
            reason = "blacklisted";
        } else {
            int match_msg = (regexec(&rx_msg_cached, cmd, 0, NULL, 0) == 0);
            int match_sync = (regexec(&rx_sync_cached, cmd, 0, NULL, 0) == 0);
            
            if (match_msg || match_sync) {
                if (is_proc_whitelisted(cmd)) {
                    should_freeze = 0;
                    #ifdef CONSOLE_DEBUG
                    printf("\033[37m%s \033[38;2;255;127;127m ¤¤ process_manager: \033[38;2;144;238;144m{Whitelisted}\033[38;2;255;127;127m : PID %d → %s\033[39m\n", debugtag(), pid, cmd);
                    #endif
                } else {
                    should_freeze = 1;
                    reason = match_msg ? "msg_pattern" : "sync_pattern";
                }
            }
        }
        
        if (should_freeze) {
            int sig = (freeze) ? SIGSTOP : SIGCONT;
            if (kill(pid, sig) == 0) {
                #ifdef CONSOLE_DEBUG
                printf("\033[37m%s \033[38;2;255;127;127m ¤¤ process_manager: %s : PID %d → %s \033[38;2;128;128;128m(%s)\033[39m\n", 
                       debugtag(), 
                       freeze ? "\033[38;2;173;216;230m{Freezed}\033[38;2;255;127;127m" : "\033[38;2;255;165;0m{Unfreezed}\033[38;2;255;127;127m", 
                       pid, cmd, reason);
                #endif
                ++count;
            } else {
                #ifdef CONSOLE_DEBUG
                printf("\033[37m%s \033[38;2;255;127;127m ¤¤ process_manager: error PID signal %d (%s)\033[39m\n", debugtag(), pid, cmd);
                #endif
            }
        }
    }
    
    closedir(dp);
    return count;
}






static void em_detect_webcams(void) {
    DIR *usb_dir;
    struct dirent *usb_entry;
    if (webcam_path) {
        free(webcam_path);
        webcam_path = NULL;
    }
    usb_dir = opendir("/sys/bus/usb/devices/");
    if (usb_dir == NULL) {
        #ifdef CONSOLE_DEBUG
        printf("\033[37m%s \033[38;2;144;238;144m \033[38;2;255;105;97mCannot access /sys/bus/usb/devices/ directory for webcam detection.\033[39m\n", debugtag());
        #endif
        return;
    }
    while ((usb_entry = readdir(usb_dir)) != NULL) {
        if (strcmp(usb_entry->d_name, ".") == 0 || strcmp(usb_entry->d_name, "..") == 0) {
            continue;
        }
        int is_webcam = 0;
        snprintf(global_buffer_256, sizeof(global_buffer_256), "/sys/bus/usb/devices/%s/product", usb_entry->d_name);
        if (access(global_buffer_256, F_OK) == 0) {
            FILE *f = fopen(global_buffer_256, "r");
            if (f) {
                char line[256];
                if (fgets(line, sizeof(line), f)) {
                    line[strcspn(line, "\n")] = '\0';
                    if (str_contains_insensitive(line, "camera") || 
                        str_contains_insensitive(line, "webcam") || 
                        str_contains_insensitive(line, "facing")) {
                        is_webcam = 1;
                    }
                }
                fclose(f);
            }
        }
        if (!is_webcam) {
            snprintf(global_buffer_256, sizeof(global_buffer_256), "/sys/bus/usb/devices/%s/bDeviceClass", usb_entry->d_name);
            if (access(global_buffer_256, F_OK) == 0) {
                FILE *f = fopen(global_buffer_256, "r");
                if (f) {
                    char line[256];
                    if (fgets(line, sizeof(line), f)) {
                        line[strcspn(line, "\n")] = '\0';
                        if (strcmp(line, "ef") == 0 || strcmp(line, "239") == 0) {
                            is_webcam = 1;
                        }
                    }
                    fclose(f);
                }
            }
        }
        if (!is_webcam) {
            snprintf(global_buffer_256, sizeof(global_buffer_256), "/sys/bus/usb/devices/%s/uevent", usb_entry->d_name);
            FILE *f = fopen(global_buffer_256, "r");
            if (f) {
                char line[256];
                while (fgets(line, sizeof(line), f)) {
                    if (str_contains_insensitive(line, "video") || 
                        str_contains_insensitive(line, "webcam") || 
                        str_contains_insensitive(line, "camera")) {
                        is_webcam = 1;
                        break;
                    }
                }
                fclose(f);
            }
        }
        if (is_webcam) {
            snprintf(global_buffer_256, sizeof(global_buffer_256), "/sys/bus/usb/devices/%s/authorized", usb_entry->d_name);
            if (access(global_buffer_256, F_OK) == 0) {
                webcam_path = strdup(usb_entry->d_name);
                if (webcam_path) {
                    #ifdef CONSOLE_DEBUG
                    printf("\033[37m%s \033[38;2;144;238;144m ►Detected webcam at %s.\033[39m\n", debugtag(), usb_entry->d_name);
                    printf("\033[37m%s \033[39m\n", debugtag());
                    #endif
                    closedir(usb_dir);
                    return;
                }
            }
        }
    }
    closedir(usb_dir);
    #ifdef CONSOLE_DEBUG
    printf("\033[37m%s \033[38;2;144;238;144m No webcam found or power control not supported.\033[39m\n", debugtag());
   printf("\033[37m%s \033[39m\n", debugtag());
    #endif
}


void terminate_cam_processes(void) {
    static const char *process_names[] = {
        "cheese", "guvcview", "skypeforlinux", "zoom", "fswebcam",
        "motion", "camorama", "kamoso", "qtcam", "webcamoid",
        "org.jitsi.jitsi-meet", "ekiga"
    };
    static const size_t process_count = sizeof(process_names) / sizeof(process_names[0]);
    DIR *dir = opendir("/proc");
    if (!dir) return;
    struct dirent *ent;
    char cmdline_path[256];
    char cmdline[512];
    pid_t self_pid = getpid();
    while ((ent = readdir(dir)) != NULL) {
        if (!is_digit_str(ent->d_name)) continue;
        pid_t pid = (pid_t)atoi(ent->d_name);
        if (pid == 0 || pid == self_pid) continue;
        snprintf(cmdline_path, sizeof(cmdline_path), "/proc/%s/cmdline", ent->d_name);
        FILE *cmdline_file = fopen(cmdline_path, "r");
        if (!cmdline_file) continue;
        if (fgets(cmdline, sizeof(cmdline), cmdline_file) != NULL) {
            for (size_t i = 0; i < process_count; i++) {
                if (strstr(cmdline, process_names[i]) != NULL) {
                    #ifdef CONSOLE_DEBUG
                    printf("\033[37m%s \033[38;2;255;127;127m ¤¤ process_manager: found webcam related process: %s (PID %d). Trying to terminate...\033[39m\n", debugtag(), process_names[i], pid);
                    #endif
                    kill(pid, SIGTERM);
                    break;
                }
            }
        }
        fclose(cmdline_file);
    }
    closedir(dir);
}


static int em_set_webcam_power(int enable) {
    int ret = 0;
    const char *val = enable ? "1" : "0";
    const char *val_txt = enable ? "enabled" : "disabled";
    if (!webcam_path) {
        return -2;
    }
    if (!enable) { terminate_cam_processes(); }
    snprintf(global_buffer_256, sizeof(global_buffer_256), "/sys/bus/usb/devices/%s/authorized", webcam_path);
    if (em_write_sysfs(global_buffer_256, val) != 0) {
        #ifdef CONSOLE_DEBUG
        printf("\033[37m%s \033[38;2;144;238;144m \033[38;2;255;105;97m  Failed to set webcam power for %s via authorized.\033[39m\n", debugtag(), webcam_path);
        printf("\033[37m%s \033[39m\n", debugtag());
        #endif
        ret = -1;
    }
    #ifdef CONSOLE_DEBUG
    printf("\033[37m%s \033[38;2;144;238;144m        !--- Webcam (%s) %s\033[39m\n", debugtag(), webcam_path,val_txt);
    printf("\033[37m%s \033[39m\n", debugtag());
    #endif
    return ret;
}






static inline int em_is_scheduler_available(const char *d, const char *s) {
    char b[512], p[128]; int f = open((snprintf(p, 128, "/sys/block/%s/queue/scheduler", d),p), O_RDONLY);
    if (f<0) return 0;
    ssize_t r = read(f, b, sizeof(b)-1); close(f);
    if (r<=0) return 0;
    while (r && (b[r-1]=='\n'||b[r-1]=='\r')) --r; b[r]=0;
    return strstr(b,s)!=0;
}



static inline int em_set_io_scheduler(const char* profile) {
    DIR *block_dir;
    struct dirent *block_entry;
    char current_scheduler[64];
    int ret = 0;
    int devices_found = 0;
    const char *target_scheduler = NULL;
    const char *fallback_scheduler = NULL;
    if (strcmp(profile, "low_power") == 0) {
        target_scheduler = "bfq";
        fallback_scheduler = "mq-deadline";
    } else if (strcmp(profile, "performance") == 0) {
        target_scheduler = "kyber";
        fallback_scheduler = "none";
    } else if (strcmp(profile, "balanced") == 0) {
        target_scheduler = "mq-deadline";
        fallback_scheduler = "mq-deadline";
    } else {
        #ifdef CONSOLE_DEBUG
        printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: \033[38;2;255;105;97mUnknown profile '%s' for I/O scheduler.\033[39m\n",debugtag(), profile);
        #endif
        return -1;
    }
    block_dir = opendir("/sys/block/");
    if (block_dir == NULL) {
        #ifdef CONSOLE_DEBUG
        printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: \033[38;2;255;105;97mCannot access /sys/block/ directory.\033[39m\n",debugtag());
        #endif
        return -1;
    }
    while ((block_entry = readdir(block_dir)) != NULL) {
        if (strcmp(block_entry->d_name, ".") == 0 || strcmp(block_entry->d_name, "..") == 0) {
            continue;
        }
        if (strncmp(block_entry->d_name, "loop", 4) == 0 ||
            strncmp(block_entry->d_name, "ram", 3) == 0 ||
            strncmp(block_entry->d_name, "dm-", 3) == 0) {
            continue;
        }
        if (strncmp(block_entry->d_name, "sd", 2) == 0 ||
            strncmp(block_entry->d_name, "nvme", 4) == 0 ||
            strncmp(block_entry->d_name, "mmc", 3) == 0 ||
            strncmp(block_entry->d_name, "hd", 2) == 0) {
            snprintf(global_buffer_256, sizeof(global_buffer_256), "/sys/block/%s/queue/scheduler", block_entry->d_name);
            if (access(global_buffer_256, F_OK) == 0) {
                devices_found++;
                if (em_read_sysfs(global_buffer_256, current_scheduler, sizeof(current_scheduler)) == 0) {
                    #ifdef CONSOLE_DEBUG
                    printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: Current scheduler for %s: %s\033[39m\n",debugtag(), block_entry->d_name, current_scheduler);
                    #endif
                }
                const char *chosen_scheduler = target_scheduler;
                if (!em_is_scheduler_available(block_entry->d_name, target_scheduler)) {
                    if (fallback_scheduler && em_is_scheduler_available(block_entry->d_name, fallback_scheduler)) {
                        chosen_scheduler = fallback_scheduler;
                        #ifdef CONSOLE_DEBUG
                        printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: \033[38;2;255;165;0mScheduler '%s' not available for %s, using '%s'.\033[39m\n",debugtag(), target_scheduler, block_entry->d_name, fallback_scheduler);
                        #endif
                    } else {
                        #ifdef CONSOLE_DEBUG
                        printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: \033[38;2;255;165;0mNeither '%s' nor '%s' available for %s, skipping.\033[39m\n",debugtag(), target_scheduler, fallback_scheduler ? fallback_scheduler : "none", block_entry->d_name);
                        #endif
                        ret = -2;
                        continue;
                    }
                }
                if (em_write_sysfs(global_buffer_256, chosen_scheduler) != 0) {
                    #ifdef CONSOLE_DEBUG
                    printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: \033[38;2;255;105;97mFailed to set I/O scheduler '%s' for %s.\033[39m\n",debugtag(), chosen_scheduler, block_entry->d_name);
                    #endif
                    ret = -1;
                } else {
                    #ifdef CONSOLE_DEBUG
                    printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: I/O scheduler '%s' set for %s.\033[39m\n",debugtag(), chosen_scheduler, block_entry->d_name);
                    #endif
                }
            }
        }
    }
    closedir(block_dir);
    if (devices_found == 0) {
        #ifdef CONSOLE_DEBUG
        printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: \033[38;2;255;165;0mNo suitable block devices found for I/O scheduler config.\033[39m\n",debugtag());
        #endif
        return -2;
    }
    #ifdef CONSOLE_DEBUG
    printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: I/O scheduler config applied to %d devices (%s).\033[39m\n",debugtag(), devices_found, profile);
    #endif
    return ret;
}



int set_wifi_power_save(const char *ifn, gboolean enable, gboolean sysfs_ok, int *ret) {
    pid_t pid = fork();
    if (pid == 0) {
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }
        char *argv[] = {
            "/usr/sbin/iw",
            "dev",
            (char *)ifn,
            "set",
            "power_save",
            (char *)(enable ? "on" : "off"),
            NULL
        };
        execv("/usr/sbin/iw", argv);
        _exit(127);
    } else if (pid > 0) {
        int status;
        waitpid(pid, &status, 0);
        int r = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        #ifdef CONSOLE_DEBUG
        const char *val_txt = enable ? "enabled" : "disabled";
        if (r == 0) {
            printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: WiFi power save %s for %s via iw.\033[39m\n",
                   debugtag(), val_txt, ifn);
        } else {
            printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: \033[38;2;255;105;97mFailed to set WiFi power save for %s via iw.\033[39m\n",
                   debugtag(), ifn);
        }
        #endif
        if (r != 0 && !sysfs_ok)
            *ret = -1;
        return r;
    }
    return -1;
}


static inline int em_set_wifi_power_save(int enable) {
    DIR *net_dir;
    struct dirent *net_entry;
    char path[PATH_MAX];
    int ret = 0;
    int wifi_devices_found = 0;
    const char *val_pm  = enable ? "auto" : "on";
    const char *val_ps  = enable ? "1"    : "0";
//    const char *val_txt = enable ? "enabled" : "disabled";
    net_dir = opendir("/sys/class/net/");
    if (net_dir == NULL) {
        #ifdef CONSOLE_DEBUG
        printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: \033[38;2;255;105;97mCannot access /sys/class/net/ directory.\033[39m\n",debugtag());
        #endif
        return -1;
    }
    while ((net_entry = readdir(net_dir)) != NULL) {
        const char *ifn = net_entry->d_name;
        if (strcmp(ifn, ".") == 0 || strcmp(ifn, "..") == 0)
            continue;
        if (strncmp(ifn, "wl", 2) != 0)
            continue;
        wifi_devices_found++;
        int sysfs_ok = 0;
        snprintf(path, sizeof(path), "/sys/class/net/%s/device/power_save", ifn);
        if (access(path, F_OK) == 0) {
            if (em_write_sysfs(path, val_ps) == 0) {
                #ifdef CONSOLE_DEBUG
                printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: WiFi power save %s for %s via device/power_save.\033[39m\n",debugtag(), enable ? "enabled" : "disabled", ifn);
                #endif
                sysfs_ok = 1;
            } else {
                #ifdef CONSOLE_DEBUG
                printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: \033[38;2;255;105;97mFailed to set WiFi power save for %s via device/power_save.\033[39m\n",debugtag(), ifn);
                #endif
                ret = -1;
            }
        } else {
            snprintf(path, sizeof(path), "/sys/class/net/%s/wireless/power_save", ifn);
            if (access(path, F_OK) == 0) {
                if (em_write_sysfs(path, val_ps) == 0) {
                    #ifdef CONSOLE_DEBUG
                    printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: WiFi power save %s for %s via wireless/power_save.\033[39m\n",debugtag(), enable ? "enabled" : "disabled", ifn);
                    #endif
                    sysfs_ok = 1;
                } else {
                    #ifdef CONSOLE_DEBUG
                    printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: \033[38;2;255;105;97mFailed to set WiFi power save for %s via wireless/power_save.\033[39m\n",debugtag(), ifn);
                    #endif
                    ret = -1;
                }
            } else {
                snprintf(path, sizeof(path), "/sys/class/net/%s/device/power/control", ifn);
                if (access(path, F_OK) == 0) {
                    if (em_write_sysfs(path, val_pm) == 0) {
                        #ifdef CONSOLE_DEBUG
                        printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: WiFi runtime PM %s for %s via device/power/control.\033[39m\n",debugtag(), enable ? "enabled" : "disabled", ifn);
                        #endif
                        sysfs_ok = 1;
                    } else {
                        #ifdef CONSOLE_DEBUG
                        printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: \033[38;2;255;105;97mFailed to set WiFi runtime PM for %s via device/power/control.\033[39m\n",debugtag(), ifn);
                        #endif
                        ret = -1;
                    }
                } else {
                    snprintf(path, sizeof(path), "/sys/class/net/%s/power/control", ifn);
                    if (access(path, F_OK) == 0) {
                        if (em_write_sysfs(path, val_pm) == 0) {
                            #ifdef CONSOLE_DEBUG
                            printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: WiFi runtime PM %s for %s via power/control.\033[39m\n",debugtag(), enable ? "enabled" : "disabled", ifn);
                             #endif
                            sysfs_ok = 1;
                        } else {
                            #ifdef CONSOLE_DEBUG
                            printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: \033[38;2;255;105;97mFailed to set WiFi runtime PM for %s via power/control.\033[39m\n",debugtag(), ifn);
                            #endif
                            ret = -1;
                        }
                    }
                }
            }
        }
        int r = set_wifi_power_save(ifn, enable, sysfs_ok, &ret);
        if (r != 0) {
            #ifdef CONSOLE_DEBUG
            printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: \033[38;2;255;105;97mFailed to set WiFi power save for %s via iw.\033[39m\n",debugtag(), ifn);
            #endif
            if (!sysfs_ok)
                ret = -1;
        }
    }
    closedir(net_dir);
    if (wifi_devices_found == 0) {
        #ifdef CONSOLE_DEBUG
        printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: \033[38;2;255;165;0mNo WiFi devices found or power save not supported.\033[39m\n",debugtag());
        #endif
        return -2;
    }
    #ifdef CONSOLE_DEBUG
    printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: WiFi power save %s on %d device(s).\033[39m\n",debugtag(), enable ? "enabled" : "disabled", wifi_devices_found);
    #endif
    return ret;
}






static inline int em_set_bluetooth_power_save(int enable) {
    DIR *bt_dir;
    struct dirent *bt_entry;
    int ret = 0;
    int bt_devices_found = 0;
    bt_dir = opendir("/sys/class/bluetooth/");
    if (bt_dir != NULL) {
        while ((bt_entry = readdir(bt_dir)) != NULL) {
            if (strcmp(bt_entry->d_name, ".") == 0 || strcmp(bt_entry->d_name, "..") == 0) {
                continue;
            }
            if (strncmp(bt_entry->d_name, "hci", 3) == 0) {
                bt_devices_found++;
                snprintf(global_buffer_256, sizeof(global_buffer_256), "/sys/class/bluetooth/%s/device/power/control", bt_entry->d_name);
                if (access(global_buffer_256, F_OK) == 0) {
                    const char *value = enable ? "auto" : "on";
                    if (em_write_sysfs(global_buffer_256, value) != 0) {
                        #ifdef CONSOLE_DEBUG
                        printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: \033[38;2;255;105;97mFailed to set BT power control for %s.\033[39m\n",debugtag(), bt_entry->d_name);
                        #endif
                        ret = -1;
                    } else {
                        #ifdef CONSOLE_DEBUG
                        printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: BT power control set to %s for %s.\033[39m\n",debugtag(), enable ? "auto" : "on", bt_entry->d_name);
                        #endif
                    }
                }
            }
        }
        closedir(bt_dir);
    }
    if (bt_devices_found == 0) {
        for (int i = 0; i < 8; i++) {
            snprintf(global_buffer_256, sizeof(global_buffer_256), "/sys/class/bluetooth/hci%d/power/control", i);
            if (access(global_buffer_256, F_OK) == 0) {
                bt_devices_found++;
                const char *value = enable ? "auto" : "on";
                if (em_write_sysfs(global_buffer_256, value) != 0) {
                    #ifdef CONSOLE_DEBUG
                    printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: \033[38;2;255;105;97mFailed to set BT power control for hci%d.\033[39m\n",debugtag(), i);
                    #endif
                    ret = -1;
                } else {
                    #ifdef CONSOLE_DEBUG
                    printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: BT power control set to %s for hci%d.\033[39m\n",debugtag(), enable ? "auto" : "on", i);
                    #endif
                }
            }
        }
    }
    if (bt_devices_found == 0) {
        #ifdef CONSOLE_DEBUG
        printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: \033[38;2;255;165;0mNo Bluetooth devices found or power control not supported.\033[39m\n",debugtag());
        #endif
        return -2;
    }
    #ifdef CONSOLE_DEBUG
    printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: Bluetooth power save %s on %d devices.\033[39m\n",debugtag(), enable ? "enabled" : "disabled", bt_devices_found);
    #endif
    return ret;
}






static inline cpu_type_t em_detect_cpu_type() {  
    char b[4096];  
    int f = open("/proc/cpuinfo", O_RDONLY);  
    if (f < 0) return CPU_TYPE_UNKNOWN;  
    ssize_t r = read(f, b, sizeof(b) - 1);  
    close(f);  
    if (r <= 0) return CPU_TYPE_UNKNOWN;  
    b[r] = 0;  
    if (strstr(b, "GenuineIntel")) return CPU_TYPE_INTEL;  
    if (strstr(b, "AuthenticAMD")) return CPU_TYPE_AMD;  
    return CPU_TYPE_UNKNOWN;  
}




static inline int em_set_intel_turbo_boost(int enable) {
    const char *p = "/sys/devices/system/cpu/intel_pstate/no_turbo";
    const char *v = enable ? "0" : "1";
    int f = open(p, O_WRONLY);
    if (f < 0) {
        #ifdef CONSOLE_DEBUG
        printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: \033[38;2;255;165;0mIntel Turbo Boost control not available (no intel_pstate).\033[39m\n",debugtag());
        #endif
        return -2;
    }
    ssize_t w = write(f, v, 1);
    close(f);
    if (w != 1) {
        #ifdef CONSOLE_DEBUG
        printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: \033[38;2;255;105;97mFailed to %s Intel Turbo Boost.\033[39m\n",debugtag(), enable ? "enable" : "disable");
        #endif
        return -1;
    }
    #ifdef CONSOLE_DEBUG
    printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: Intel Turbo Boost %s.\033[39m\n",debugtag(), enable ? "enabled" : "disabled");
    #endif
    return 0;
}




static inline int em_set_amd_boost(int enable) {
    const char *p = "/sys/devices/system/cpu/cpufreq/boost";
    const char *v = enable ? "1" : "0";
    int f = open(p, O_WRONLY);
    if (f < 0) {
        #ifdef CONSOLE_DEBUG
        printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: \033[38;2;255;165;0mAMD Boost control not available.\033[39m\n",debugtag());
        #endif
        return -2;
    }
    ssize_t w = write(f, v, 1);
    close(f);
    if (w != 1) {
        #ifdef CONSOLE_DEBUG
        printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: \033[38;2;255;105;97mFailed to %s AMD Boost.\033[39m\n",debugtag(), enable ? "enable" : "disable");
        #endif
        return -1;
    }
    #ifdef CONSOLE_DEBUG
    printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: AMD Boost %s.\033[39m\n",debugtag(), enable ? "enabled" : "disabled");
    #endif
    return 0;
}




static inline int em_set_cpu_boost(int enable) {
    #ifdef CONSOLE_DEBUG
    printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: CPU type: %s\033[39m\n",debugtag(), cpu_type == CPU_TYPE_INTEL ? "Intel" : cpu_type == CPU_TYPE_AMD ? "AMD" : "Unknown");
    #endif
    if (cpu_type == CPU_TYPE_INTEL) return em_set_intel_turbo_boost(enable);
    if (cpu_type == CPU_TYPE_AMD) return em_set_amd_boost(enable);
    #ifdef CONSOLE_DEBUG
    printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: \033[38;2;255;165;0mUnknown CPU type, cannot control boost.\033[39m\n",debugtag());
    #endif
    return -2;
}



static inline int em_set_sata_link_power_management(const char *policy) {
    DIR *dir;
    struct dirent *entry;
    int ret = 0;
    int devices_found = 0;
    dir = opendir("/sys/class/scsi_host/");
    if (dir == NULL) {
        #ifdef CONSOLE_DEBUG
        printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: \033[38;2;255;105;97mCannot access SATA host directory.\033[39m\n",debugtag());
        #endif
        return -1;
    }
    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, "host", 4) == 0) {
            snprintf(global_buffer_256, sizeof(global_buffer_256), "/sys/class/scsi_host/%s/link_power_management_policy", entry->d_name);
            if (access(global_buffer_256, F_OK) == 0) {
                devices_found++;
                if (em_write_sysfs(global_buffer_256, policy) != 0) {
                    #ifdef CONSOLE_DEBUG
                    printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: \033[38;2;255;105;97mFailed to set SATA link power policy for %s.\033[39m\n",debugtag(), entry->d_name);
                    #endif
                    ret = -1;
                } else {
                    #ifdef CONSOLE_DEBUG
                    printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: SATA link power policy set to '%s' for %s.\033[39m\n",debugtag(), policy, entry->d_name);
                    #endif
                }
            }
        }
    }
    closedir(dir);
    if (devices_found == 0) {
        #ifdef CONSOLE_DEBUG
        printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: \033[38;2;255;165;0mNo SATA devices found for link power management.\033[39m\n",debugtag());
        #endif
        return -2;
    }
    return ret;
}




static inline int em_set_runtime_pm_for_all_devices(int enable) {
    DIR *dev_dir;
    struct dirent *dev_entry;
    int ret = 0;
    int devices_count = 0;
    const char *mode = enable ? "auto" : "on";
    dev_dir = opendir("/sys/bus/pci/devices/");
    if (dev_dir != NULL) {
        while ((dev_entry = readdir(dev_dir)) != NULL) {
            if (strcmp(dev_entry->d_name, ".") == 0 || strcmp(dev_entry->d_name, "..") == 0) {
                continue;
            }
            snprintf(global_buffer_256, sizeof(global_buffer_256), "/sys/bus/pci/devices/%s/power/control", dev_entry->d_name);
            if (access(global_buffer_256, F_OK) == 0) {
                devices_count++;
                if (em_write_sysfs(global_buffer_256, mode) != 0) {
                    #ifdef CONSOLE_DEBUG
                    printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: \033[38;2;255;105;97mFailed to set runtime PM to '%s' for %s.\033[39m\n",debugtag(), mode, dev_entry->d_name);
                    #endif
                    ret = -1;
                }
            }
        }
        closedir(dev_dir);
    } else {
        #ifdef CONSOLE_DEBUG
        printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: \033[38;2;255;105;97mCannot access PCI devices directory.\033[39m\n",debugtag());
        #endif
        ret = -1;
    }
    #ifdef CONSOLE_DEBUG
    printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: Runtime PM %s for %d PCI devices.\033[39m\n",debugtag(), enable ? "enabled" : "disabled", devices_count);
    #endif
    return ret;
}



static inline int em_get_max_available_freq(const char *policy_path, unsigned long *max_freq) {
    char buffer[256];
    snprintf(global_buffer_256, sizeof(global_buffer_256), "%s/scaling_available_frequencies", policy_path);
    if (access(global_buffer_256, F_OK) == 0) {
        if (em_read_sysfs(global_buffer_256, buffer, sizeof(buffer)) == 0) {
            unsigned long max = 0;
            char *buffer_ptr = buffer;
            char *token;
            #ifdef CONSOLE_DEBUG
            printf("\033[37m%s \033[38;2;144;238;144m Available frequencies from %s: ",debugtag(), global_buffer_256);
            #endif
            while ((token = strsep(&buffer_ptr, " ")) != NULL) {
                if (*token == '\0') continue;
                unsigned long freq = strtoul(token, NULL, 10);
                #ifdef CONSOLE_DEBUG
                printf("%lu ", freq);
                #endif
                if (freq > max) {
                    max = freq;
                }
            }
            #ifdef CONSOLE_DEBUG
            printf("\n");
            #endif
            if (max != 0) {
                *max_freq = max;
                return 0;
            }
            #ifdef CONSOLE_DEBUG
            printf("\033[37m%s \033[38;2;144;238;144m \033[38;2;255;105;97mNo valid frequencies found in scaling_available_frequencies (%s).\033[39m\n",debugtag(), global_buffer_256);
            #endif
        } else {
            #ifdef CONSOLE_DEBUG
            printf("\033[37m%s \033[38;2;144;238;144m \033[38;2;255;105;97mError reading scaling_available_frequencies from %s.\033[39m\n",debugtag(), global_buffer_256);
            #endif
        }
    }
    snprintf(global_buffer_256, sizeof(global_buffer_256), "/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq");
    if (em_read_sysfs_ul(global_buffer_256, max_freq) != 0) {
        #ifdef CONSOLE_DEBUG
        printf("\033[37m%s \033[38;2;144;238;144m \033[38;2;255;105;97mError reading max frequency from %s. Using default (2000MHz).\033[39m\n",debugtag(), global_buffer_256);
        #endif
        *max_freq = 2000000;
    }
    unsigned long min_freq;
    snprintf(global_buffer_256, sizeof(global_buffer_256), "/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_min_freq");
    if (em_read_sysfs_ul(global_buffer_256, &min_freq) != 0) {
        #ifdef CONSOLE_DEBUG
        printf("\033[37m%s \033[38;2;144;238;144m \033[38;2;255;105;97mError reading min frequency from %s. Using default (400MHz).\033[39m\n",debugtag(), global_buffer_256);
        #endif
        min_freq = 400000;
    }
    #ifdef CONSOLE_DEBUG
    printf("\033[37m%s \033[38;2;144;238;144m ►Successfully read CPU min frequency: %lu KHz.\033[39m\n",debugtag(), min_freq);
    printf("\033[37m%s \033[38;2;144;238;144m ►Successfully read CPU max frequency: %lu KHz.\033[39m\n",debugtag(), *max_freq);
    printf("\033[37m%s\033[39m\n",debugtag());
    #endif
    return 0;
}





static inline int em_set_cpu_governor(const char *governor) {
    DIR *dir;
    struct dirent *entry;
    int ret = 0;
    int policies_found = 0;
    snprintf(global_buffer_256, sizeof(global_buffer_256), "/sys/devices/system/cpu/cpufreq/policy0/scaling_available_governors");
    if (access(global_buffer_256, F_OK) != 0) {
        #ifdef CONSOLE_DEBUG 
        printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: \033[38;2;255;105;97m(WARNING) CPUfreq policy0 not found. Cannot determine available governors.\033[39m\n",debugtag());
        #endif
        return -1;
    }
    char available_governors[256];
    if (em_read_sysfs(global_buffer_256, available_governors, sizeof(available_governors)) != 0) {
        #ifdef CONSOLE_DEBUG 
        printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: \033[38;2;255;105;97m(WARNING) Failed to read available governors from %s. Skipping governor setup.\033[39m\n",debugtag(), global_buffer_256);
        #endif
        return -1;
    }
    if (!strstr(available_governors, governor)) {
        #ifdef CONSOLE_DEBUG 
        printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: \033[38;2;255;165;0m(WARNING) Governor '%s' not available on this system. Cannot set.\033[39m\n",debugtag(), governor);
        #endif
        return -2;
    }
    dir = opendir("/sys/devices/system/cpu/cpufreq/");
    if (dir == NULL) {
        perror("energy_manager: opendir /sys/devices/system/cpu/cpufreq/");
        return -1;
    }
    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, "policy", 6) == 0 && entry->d_name[6] >= '0' && entry->d_name[6] <= '9') {
            policies_found++;
            snprintf(global_buffer_256, sizeof(global_buffer_256), "/sys/devices/system/cpu/cpufreq/%s/scaling_governor", entry->d_name);
            if (em_write_sysfs(global_buffer_256, governor) != 0) {
                #ifdef CONSOLE_DEBUG 
                printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: \033[38;2;255;105;97mFailed to set governor for %s. (Path: %s)\033[39m\n",debugtag(), entry->d_name, global_buffer_256);
                #endif
                ret = -1;
            }
        }
    }
    closedir(dir);
    #ifdef CONSOLE_DEBUG
    printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: Governor '%s' applied to %d CPU policies.\033[39m\n",debugtag(), governor, policies_found);
    #endif
    return ret;
}




static inline int em_set_cpu_frequency_range(unsigned long min_freq_khz, unsigned long max_freq_khz) {
    char freq_str[32];
    DIR *dir;
    struct dirent *entry;
    int ret = 0;
    int policies_found = 0;
    dir = opendir("/sys/devices/system/cpu/cpufreq/");
    if (dir == NULL) {
        perror("energy_manager: opendir /sys/devices/system/cpu/cpufreq/");
        return -1;
    }
    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, "policy", 6) == 0 && entry->d_name[6] >= '0' && entry->d_name[6] <= '9') {
            policies_found++;
            snprintf(global_buffer_256, sizeof(global_buffer_256), "/sys/devices/system/cpu/cpufreq/%s/scaling_min_freq", entry->d_name);
            snprintf(freq_str, sizeof(freq_str), "%lu", min_freq_khz);
            if (em_write_sysfs(global_buffer_256, freq_str) != 0) {
                #ifdef CONSOLE_DEBUG 
                printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: \033[38;2;255;105;97mFailed to set min_freq for %s. (Path: %s)\033[39m\n",debugtag(), entry->d_name, global_buffer_256);
                #endif
                ret = -1;
            }
            snprintf(global_buffer_256, sizeof(global_buffer_256), "/sys/devices/system/cpu/cpufreq/%s/scaling_max_freq", entry->d_name);
            snprintf(freq_str, sizeof(freq_str), "%lu", max_freq_khz);
            if (em_write_sysfs(global_buffer_256, freq_str) != 0) {
                #ifdef CONSOLE_DEBUG 
                printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: \033[38;2;255;105;97mFailed to set max_freq for %s. (Path: %s)\033[39m\n",debugtag(), entry->d_name, global_buffer_256);
                #endif
                ret = -1;
            }
        }
    }
    closedir(dir);
    #ifdef CONSOLE_DEBUG
    printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: Frequency range %lu-%lu KHz applied to %d CPU policies.\033[39m\n",debugtag(), min_freq_khz, max_freq_khz, policies_found);
    #endif
    return ret;
}


static inline int em_set_intel_cpu_perf_pct(int min_pct, int max_pct) {
    const char *intel_pstate_path = "/sys/devices/system/cpu/intel_pstate";
    const char *min_path = "/sys/devices/system/cpu/intel_pstate/min_perf_pct";
    const char *max_path = "/sys/devices/system/cpu/intel_pstate/max_perf_pct";
    if (access(intel_pstate_path, F_OK) != 0) {
        #ifdef CONSOLE_DEBUG
        printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: \033[38;2;255;165;0mIntel P-state driver not available.\033[39m\n",debugtag());
        #endif
        return -2;
    }
    int error_count = 0;
    // --- Min perf %
    if (access(min_path, F_OK) == 0) {
        char min_str[8];
        snprintf(min_str, sizeof(min_str), "%d", min_pct);
        int fd = open(min_path, O_WRONLY);
        if (fd >= 0) {
            ssize_t w = write(fd, min_str, strlen(min_str));
            close(fd);
            if (w == (ssize_t)strlen(min_str)) {
                #ifdef CONSOLE_DEBUG
                printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: Intel min_perf_pct set to %d%%.\033[39m\n",debugtag(), min_pct);
                #endif
            } else {
                error_count++;
                #ifdef CONSOLE_DEBUG
                printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: \033[38;2;255;105;97mFailed to set min_perf_pct to %d%%.\033[39m\n",debugtag(), min_pct);
                #endif
            }
        } else {
            error_count++;
        }
    } else {
        #ifdef CONSOLE_DEBUG
        printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: min_perf_pct is not supported by Intel P-state driver.\033[39m\n",debugtag());
        #endif
    }
    // max perf %
    if (access(max_path, F_OK) == 0) {
        char max_str[8];
        snprintf(max_str, sizeof(max_str), "%d", max_pct);
        int fd = open(max_path, O_WRONLY);
        if (fd >= 0) {
            ssize_t w = write(fd, max_str, strlen(max_str));
            close(fd);
            if (w == (ssize_t)strlen(max_str)) {
                #ifdef CONSOLE_DEBUG
                printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: Intel max_perf_pct set to %d%%.\033[39m\n",debugtag(), max_pct);
                #endif
            } else {
                error_count++;
                #ifdef CONSOLE_DEBUG
                printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: \033[38;2;255;105;97mFailed to set max_perf_pct to %d%%.\033[39m\n",debugtag(), max_pct);
                #endif
            }
        } else {
            error_count++;
        }
    } else {
        #ifdef CONSOLE_DEBUG
        printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: max_perf_pct is not supported by Intel P-state driver.\033[39m\n",debugtag());
        #endif
    }
    return (error_count == 0) ? 0 : -1;
}








static inline int em_set_amd_cpu_perf_pct(int min_pct, int max_pct) {
    const char *amd_pstate_path = "/sys/devices/system/cpu/amd_pstate";
    char min_path[128], max_path[128];
    int error_count = 0;
    if (access(amd_pstate_path, F_OK) != 0) {
        #ifdef CONSOLE_DEBUG
        printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: \033[38;2;255;165;0mAMD P-state driver not available.\033[39m\n",debugtag());
        #endif
        return -2;
    }
    DIR *dir = opendir("/sys/devices/system/cpu");
    if (!dir) return -1;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, "policy", 6) == 0 && my_isdigit(entry->d_name[6])) {
            snprintf(min_path, sizeof(min_path), "/sys/devices/system/cpu/cpufreq/%s/scaling_min_freq", entry->d_name);
            snprintf(max_path, sizeof(max_path), "/sys/devices/system/cpu/cpufreq/%s/scaling_max_freq", entry->d_name);
            char min_str[16], max_str[16];
            snprintf(min_str, sizeof(min_str), "%d000", min_pct);
            snprintf(max_str, sizeof(max_str), "%d000", max_pct);
            int min_fd = open(min_path, O_WRONLY);
            if (min_fd >= 0) {
                ssize_t w = write(min_fd, min_str, strlen(min_str));
                close(min_fd);
                if (w != (ssize_t)strlen(min_str)) error_count++;
                else {
                    #ifdef CONSOLE_DEBUG
                    printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: AMD min_perf set to %s KHz.\033[39m\n",debugtag(), min_str);
                    #endif
                }
            } else {
                error_count++;
            }
            int max_fd = open(max_path, O_WRONLY);
            if (max_fd >= 0) {
                ssize_t w = write(max_fd, max_str, strlen(max_str));
                close(max_fd);
                if (w != (ssize_t)strlen(max_str)) error_count++;
                else {
                    #ifdef CONSOLE_DEBUG
                    printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: AMD max_perf set to %s KHz.\033[39m\n",debugtag(), max_str);
                    #endif
                }
            } else {
                error_count++;
            }
        }
    }
    closedir(dir);
    return (error_count == 0) ? 0 : -1;
}




static inline int em_set_pstate_epp(const char *epp_value) {
    DIR *dir;
    struct dirent *entry;
    int ret = 0;
    int policies_found = 0;
    if (access("/sys/devices/system/cpu/intel_pstate", F_OK) != 0) {
        #ifdef CONSOLE_DEBUG
        printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: \033[38;2;255;165;0mIntel P-state driver not available.\033[39m\n",debugtag());
        #endif
        return -2;
    }
    dir = opendir("/sys/devices/system/cpu/cpufreq/");
    if (dir == NULL) {
        return -1;
    }
    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, "policy", 6) == 0 && entry->d_name[6] >= '0' && entry->d_name[6] <= '9') {
            snprintf(global_buffer_256, sizeof(global_buffer_256), "/sys/devices/system/cpu/cpufreq/%s/energy_performance_preference", entry->d_name);
            if (access(global_buffer_256, F_OK) == 0) {
                policies_found++;
                if (em_write_sysfs(global_buffer_256, epp_value) != 0) {
                    #ifdef CONSOLE_DEBUG 
                    printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: \033[38;2;255;105;97mFailed to set P-state EPP to '%s' for %s.\033[39m\n",debugtag(), epp_value, entry->d_name);
                    #endif
                    ret = -1;
                }
            }
        }
    }
    closedir(dir);
    #ifdef CONSOLE_DEBUG
    if (policies_found > 0) {
        printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: P-state EPP '%s' applied to %d CPU policies.\033[39m\n",debugtag(), epp_value, policies_found);
    } else {
        printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: \033[38;2;255;165;0mNo P-state EPP interfaces found.\033[39m\n",debugtag());
    }
    #endif
    return (policies_found > 0) ? ret : -2;
}




static inline int em_set_pcie_aspm(const char *policy) {
    const char *aspm_path = "/sys/module/pcie_aspm/parameters/policy";
    if (access(aspm_path, F_OK) != 0) {
        #ifdef CONSOLE_DEBUG
        printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: \033[38;2;255;165;0mPCIe ASPM policy interface not available.\033[39m\n", debugtag());
        #endif
        return -2;
    }
    if (em_write_sysfs(aspm_path, policy) != 0) {
        #ifdef CONSOLE_DEBUG
        printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: \033[38;2;255;105;97mFailed to set PCIe ASPM policy to '%s'.\033[39m\n", debugtag(), policy);
        #endif
        return -1;
    }
    #ifdef CONSOLE_DEBUG
    printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: PCIe ASPM policy set to '%s'.\033[39m\n", debugtag(), policy);
    #endif
    return 0;
}





static int is_usb_hid_or_audio(const char *devname) {
    char dev_path[PATH_MAX], iface_path[PATH_MAX], class_buf[8];
    snprintf(dev_path, sizeof(dev_path), "/sys/bus/usb/devices/%s", devname);
    DIR *dev_dir = opendir(dev_path);
    if (!dev_dir) return 0;
    struct dirent *entry;
    while ((entry = readdir(dev_dir)) != NULL) {
        if (!strchr(entry->d_name, ':')) continue;
        snprintf(iface_path, sizeof(iface_path), "%s/%s/bInterfaceClass", dev_path, entry->d_name);
        if (em_read_sysfs(iface_path, class_buf, sizeof(class_buf)) == 0) {
            if (strcmp(class_buf, "03") == 0 || strcmp(class_buf, "01") == 0) {
                closedir(dev_dir);
                return 1;
            }
        }
    }
    closedir(dev_dir);
    return 0;
}

static inline int em_set_usb_autosuspend(int enable) {
    DIR *dev_dir = opendir("/sys/bus/usb/devices/");
    if (dev_dir == NULL) {
        #ifdef CONSOLE_DEBUG
        printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: \033[38;2;255;105;97mCannot access USB devices directory.\033[39m\n",debugtag());
        #endif
        return -1;
    }
    struct dirent *dev_entry;
    int ret = 0;
    int devices_found = 0;
    int skipped_hid_audio = 0;
    const char *mode = (enable == 1) ? "auto" : "on";
    while ((dev_entry = readdir(dev_dir)) != NULL) {
        if (strcmp(dev_entry->d_name, ".") == 0 || strcmp(dev_entry->d_name, "..") == 0 ||
            strncmp(dev_entry->d_name, "usb", 3) == 0 || strchr(dev_entry->d_name, ':') != NULL) {
            continue;
        }
        char ctrl_path[PATH_MAX];
        snprintf(ctrl_path, sizeof(ctrl_path), "/sys/bus/usb/devices/%s/power/control", dev_entry->d_name);
        if (access(ctrl_path, F_OK) != 0) continue;
        if (enable && is_usb_hid_or_audio(dev_entry->d_name)) {
            skipped_hid_audio++;
            #ifdef CONSOLE_DEBUG
            printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: \033[38;2;255;165;0mSkipping HID/Audio USB device %s from autosuspend.\033[39m\n", debugtag(), dev_entry->d_name);
            #endif
            continue;
        }
        devices_found++;
        if (em_write_sysfs(ctrl_path, mode) != 0) {
            #ifdef CONSOLE_DEBUG
            printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: \033[38;2;255;105;97mFailed to set USB autosuspend to '%s' for %s.\033[39m\n",debugtag(), mode, dev_entry->d_name);
            #endif
            ret = -1;
        }
    }
    closedir(dev_dir);
    #ifdef CONSOLE_DEBUG
    printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: USB autosuspend %s for %d devices (skipped %d HID/Audio).\033[39m\n",debugtag(), enable ? "enabled" : "disabled", devices_found, skipped_hid_audio);
    #endif
    return ret;
}





static inline int em_set_dirty_parameters(int enable)  {  //0=AC ; 1=eco
    int max_lost_work_secs = enable ? 60 : 15;
    int cage = max_lost_work_secs * 100;
    int ret = 0;
    snprintf(global_buffer_256, sizeof(global_buffer_256), "%d", cage);
    for (size_t i = 0; i < sizeof(dirty_params_paths)/sizeof(dirty_params_paths[0]); ++i) {
        if (access(dirty_params_paths[i], F_OK) == 0) {
            if (em_write_sysfs(dirty_params_paths[i], global_buffer_256) != 0) {
                  #ifdef CONSOLE_DEBUG
                   printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: \033[38;2;255;105;97m Dirty parameters (%d): write_error %s %d\033[39m\n",debugtag(), enable, dirty_params_paths[i], cage);
                  #endif
                ret = -1;
            }
        }
    }
    if (access(xfsbufd_path, F_OK) == 0) {
        if (em_write_sysfs(xfsbufd_path, "3000") != 0) {
            #ifdef CONSOLE_DEBUG
            printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: \033[38;2;255;105;97m Dirty parameters (%d): write_error %s 3000\033[39m\n",debugtag(), enable, xfsbufd_path);
            #endif
            ret = -1;
        }
    }
    #ifdef CONSOLE_DEBUG
    printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: Dirty parameters configured: cage=%d.\033[39m\n",debugtag(), cage);
    #endif
    return ret;
}



static inline int em_set_laptopmode(int enable) {
    int isec = enable ? 2 : 0;
    snprintf(global_buffer_256, sizeof(global_buffer_256), "%d", isec);
    const char *laptopmode_path = "/proc/sys/vm/laptop_mode";
    int rc = 0;
    if (access(laptopmode_path, F_OK) == 0) {
        rc = em_write_sysfs(laptopmode_path, global_buffer_256);
        #ifdef CONSOLE_DEBUG
        printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: Laptop mode configured: %d\033[39m\n",debugtag(), isec);
        #endif
    } else {
        #ifdef CONSOLE_DEBUG
           printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: \033[38;2;255;105;97m Laptop mode %s (not found)\033[39m\n",debugtag(), laptopmode_path);
        #endif
        rc = -1;
    }
    return rc;
}





static inline int em_set_sound_power_save(int enable) {
    int ret = 0;
    int devices_found = 0;
    const char *value = enable ? "1" : "0";
    const char *controller_value = enable ? "Y" : "N";
    const char *sound_power_paths[] = { 
        "/sys/module/snd_hda_intel/parameters/power_save",
        "/sys/module/snd_ac97_codec/parameters/power_save",
        "/sys/module/snd_sof/parameters/power_save",
        NULL
    };
    const char *controller_path = "/sys/module/snd_hda_intel/parameters/power_save_controller";
    for (int i = 0; sound_power_paths[i] != NULL; i++) {
        if (access(sound_power_paths[i], F_OK) == 0) {
            devices_found++;
            if (em_write_sysfs(sound_power_paths[i], value) != 0) {
                #ifdef CONSOLE_DEBUG
                printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: \033[38;2;255;105;97mFailed to set powersave to '%s' for %s\033[39m\n", debugtag(), value, sound_power_paths[i]);
                #endif
                ret = -1;
            } else {
                #ifdef CONSOLE_DEBUG
                printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: powersave %s %s\033[39m\n", debugtag(), enable ? "enabled" : "disabled", sound_power_paths[i]);
                #endif
            }
        }
    }
    if (access(controller_path, F_OK) == 0) {
        if (em_write_sysfs(controller_path, controller_value) != 0) {
            #ifdef CONSOLE_DEBUG
            printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: \033[38;2;255;105;97mFailed to set power_save_controller to '%s'\033[39m\n", debugtag(), controller_value);
            #endif
            ret = -1;
        } else {
            #ifdef CONSOLE_DEBUG
            printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: power_save_controller %s\033[39m\n", debugtag(), enable ? "enabled" : "disabled");
            #endif
        }
    }
    if (devices_found == 0) {
        #ifdef CONSOLE_DEBUG
        printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: \033[38;2;255;165;0mNo sound devices found or powersave not supported.\033[39m\n", debugtag());
        #endif
        return -2;
    }
    #ifdef CONSOLE_DEBUG
    printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: sound powersave %s on %d device(s).\033[39m\n", debugtag(), enable ? "enabled" : "disabled", devices_found);
    #endif
    return ret;
}



void set_platform_profile(const char *profile) {
    const char *available_path = NULL;
    for (int i = 0; platform_profile_paths[i] != NULL; i++) {
        if (access(platform_profile_paths[i], F_OK) == 0) {
            available_path = platform_profile_paths[i];
            #ifdef CONSOLE_DEBUG
            printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: Found platform_profile at %s\033[39m\n",debugtag(), available_path);
            #endif
            break;
        }
    }
    if (available_path != NULL) {
         if (em_write_sysfs(available_path, profile) != 0) {
             #ifdef CONSOLE_DEBUG
             printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: Failed to set platform_profile to %s at %s\033[39m\n",debugtag(), profile, available_path);
             #endif
        } else {
            #ifdef CONSOLE_DEBUG
            printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: # platform_profile set to %s at %s\033[39m\n",debugtag(), profile, available_path);
            #endif
        }
    } else {
        #ifdef CONSOLE_DEBUG
        printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: \033[38;2;255;165;0mno platform_profile available\033[39m\n",debugtag());
        #endif
    }
}




static inline int em_set_gpu_power_profile(const char *gpuprofile) {
    int ret = 0, devices_found = 0;
    // amd (/sys/class/drm)
    DIR *drm_dir = opendir("/sys/class/drm/");
    if (drm_dir) {
        struct dirent *entry;
        while ((entry = readdir(drm_dir)) != NULL) {
            if (strncmp(entry->d_name, "card", 4) != 0)
                continue;
            char amd_path[PATH_MAX];
            snprintf(amd_path, sizeof(amd_path), "/sys/class/drm/%s/device/power_dpm_force_performance_level", entry->d_name);
            if (access(amd_path, W_OK) == 0) {
                devices_found++;
                const char *val = NULL;
                if (strcmp(gpuprofile, "low_power") == 0) val = "low";
                else if (strcmp(gpuprofile, "balanced") == 0) val = "auto";
                else if (strcmp(gpuprofile, "performance") == 0) val = "high";
                else val = "auto";
                if (em_write_sysfs(amd_path, val) != 0) {
                    #ifdef CONSOLE_DEBUG
                    printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: \033[38;2;255;165;0mFailed to set AMD GPU profile '%s' for %s.\033[39m\n",debugtag(), val, entry->d_name);
                    #endif
                    ret = -1;
                } else {
                    #ifdef CONSOLE_DEBUG
                    printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager:  AMD GPU profile '%s' set for %s.\033[39m\n",debugtag(), val, entry->d_name);
                    #endif
                }
            }
        }
        closedir(drm_dir);
    }
    // intel
    DIR *pci_dir = opendir("/sys/devices/");
    if (pci_dir) {
        struct dirent *pci_entry;
        while ((pci_entry = readdir(pci_dir)) != NULL) {
            if (strncmp(pci_entry->d_name, "pci", 3) != 0)
                continue;
            char pci_path[PATH_MAX];
            snprintf(pci_path, sizeof(pci_path), "/sys/devices/%s", pci_entry->d_name);
            DIR *dev_dir = opendir(pci_path);
            if (!dev_dir) continue;
            struct dirent *dev_entry;
            while ((dev_entry = readdir(dev_dir)) != NULL) {
                if (dev_entry->d_name[0] == '.') continue;
                char drm_path[PATH_MAX];
                snprintf(drm_path, sizeof(drm_path), "%s/%s/drm", pci_path, dev_entry->d_name);
                DIR *drm_subdir = opendir(drm_path);
                if (!drm_subdir) continue;
                struct dirent *drm_entry;
                while ((drm_entry = readdir(drm_subdir)) != NULL) {
                    if (strncmp(drm_entry->d_name, "card", 4) != 0)
                        continue;
                    char min_path[PATH_MAX], max_path[PATH_MAX], boost_path[PATH_MAX];
                    snprintf(min_path, sizeof(min_path), "%s/%s/gt_min_freq_mhz", drm_path, drm_entry->d_name);
                    snprintf(max_path, sizeof(max_path), "%s/%s/gt_max_freq_mhz", drm_path, drm_entry->d_name);
                    snprintf(boost_path, sizeof(boost_path), "%s/%s/gt_boost_freq_mhz", drm_path, drm_entry->d_name);
                    #ifdef CONSOLE_DEBUG
                    printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: Intel GPU freq path:\033[39m\n",debugtag());
                    printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: %s\033[39m\n",debugtag(), min_path);
                    #endif
                    if (access(min_path, W_OK) == 0 && access(max_path, W_OK) == 0) {
                        devices_found++;
                        unsigned long min_freq = 0, max_freq = 0, boost_freq = 0, new_max = 0;
                        char max_buf[32];
                        if (em_read_sysfs_ul(min_path, &min_freq) != 0) min_freq = 100;
                        if (em_read_sysfs_ul(max_path, &max_freq) != 0) max_freq = 1000;
                        if (access(boost_path, R_OK) == 0 && em_read_sysfs_ul(boost_path, &boost_freq) == 0)
                            if (boost_freq > max_freq) max_freq = boost_freq;
                          if (strcmp(gpuprofile, "low_power") == 0)
                              new_max = min_freq;
                          else if (strcmp(gpuprofile, "balanced") == 0)
                              new_max = (min_freq + max_freq) / 2;
                          else if (strcmp(gpuprofile, "performance") == 0)
                              new_max = max_freq;
                          else
                              new_max = max_freq;
                        snprintf(max_buf, sizeof(max_buf), "%lu", new_max);
                        if (em_write_sysfs(max_path, max_buf) != 0) {
                            #ifdef CONSOLE_DEBUG
                            printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: \033[38;2;255;165;0mFailed to set Intel GPU max_freq %lu for %s.\033[39m\n",debugtag(), new_max, drm_entry->d_name);
                            #endif
                            ret = -1;
                        } else {
                            #ifdef CONSOLE_DEBUG
                            printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: Intel GPU max_freq set to %lu MHz for %s (profile: %s).\033[39m\n",debugtag(), new_max, drm_entry->d_name, gpuprofile);
                            #endif
                        }
                    }
                }
                closedir(drm_subdir);
            }
            closedir(dev_dir);
        }
        closedir(pci_dir);
    }
    if (devices_found == 0) {
        #ifdef CONSOLE_DEBUG
        printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: \033[38;2;255;165;0mCan't set power profile for GPU device.\033[39m\n",debugtag());
        #endif
        return -2;
    }
    return ret;
}




static inline int em_set_cpu_perf_policy(const char *perf) {
    int cnt = 0, err_cnt = 0;
    // epp (intel/amd)
    DIR *dir = opendir("/sys/devices/system/cpu");
    if (!dir) return -1;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, "cpu", 3) == 0 && my_isdigit(entry->d_name[3])) {
            snprintf(global_buffer_256, sizeof(global_buffer_256), "/sys/devices/system/cpu/%s/cpufreq/energy_performance_preference", entry->d_name);
            if (access(global_buffer_256, F_OK) == 0) {
                int fd = open(global_buffer_256, O_WRONLY);
                if (fd >= 0) {
                    ssize_t w = write(fd, perf, strlen(perf));
                    close(fd);
                    cnt++;
                    if (w != (ssize_t)strlen(perf)) err_cnt++;
                }
            }
        }
    }
    closedir(dir);
    #ifdef CONSOLE_DEBUG
    if (cnt > 0) {
        if (err_cnt == 0) {
            printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: CPU EPP '%s' applied to %d cores.\033[39m\n",debugtag(), perf, cnt);
        } else {
            printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: \033[38;2;255;105;97mFailed to apply EPP '%s' on %d of %d cores.\033[39m\n",debugtag(), perf, err_cnt, cnt);
        }
    } else {
        printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: \033[38;2;255;165;0mNo EPP-capable interfaces found.\033[39m\n",debugtag());
    }
    #endif
    // epb (intel only)
    cnt = 0; err_cnt = 0;
    int pnum = -1;
    if      (strcmp(perf, "performance") == 0)         pnum = 0;
    else if (strcmp(perf, "balance_performance") == 0) pnum = 4;
    else if (strcmp(perf, "default") == 0)             pnum = 6;
    else if (strcmp(perf, "balance_power") == 0)       pnum = 8;
    else if (strcmp(perf, "power") == 0)               pnum = 15;
    else {
        int val = atoi(perf);
        if (val >= 0 && val <= 15) pnum = val;
    }
    if (pnum != -1) {
        dir = opendir("/sys/devices/system/cpu");
        if (dir) {
            while ((entry = readdir(dir)) != NULL) {
                if (strncmp(entry->d_name, "cpu", 3) == 0 && my_isdigit(entry->d_name[3])) {
                    snprintf(global_buffer_256, sizeof(global_buffer_256), "/sys/devices/system/cpu/%s/power/energy_perf_bias", entry->d_name);
                    if (access(global_buffer_256, F_OK) == 0) {
                        int fd = open(global_buffer_256, O_WRONLY);
                        if (fd >= 0) {
                            char b[4]; snprintf(b, sizeof(b), "%d", pnum);
                            ssize_t w = write(fd, b, strlen(b));
                            close(fd);
                            cnt++;
                            if (w != (ssize_t)strlen(b)) err_cnt++;
                        }
                    }
                }
            }
            closedir(dir);
        }
        #ifdef CONSOLE_DEBUG
        if (cnt > 0) {
            if (err_cnt == 0) {
                printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: CPU EPB '%s(%d)' applied to %d cores.\033[39m\n",debugtag(), perf, pnum, cnt);
            } else {
                printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: \033[38;2;255;105;97mFailed to apply EPB '%s(%d)' on %d of %d cores.\033[39m\n",debugtag(), perf, pnum, err_cnt, cnt);
            }
        } else {
            printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: EPB not available or unsupported.\033[39m\n",debugtag());
        }
        #endif
    } else {
        #ifdef CONSOLE_DEBUG
        printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: \033[38;2;255;165;0mInvalid EPB/EPP value '%s'.\033[39m\n",debugtag(), perf);
        #endif
        return -1;
    }
    return 0;
}



static inline int em_set_cpu_dyn_boost(int enable) {
    const char *boost_path = "/sys/devices/system/cpu/intel_pstate/hwp_dynamic_boost";
    const char *intel_pstate_path = "/sys/devices/system/cpu/intel_pstate";
    const char *value = enable ? "1" : "0";
    if (access(intel_pstate_path, F_OK) != 0) {
        #ifdef CONSOLE_DEBUG
        printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: \033[38;2;255;165;0mIntel P-state driver not available.\033[39m\n",debugtag());
        #endif
        return -2;
    }
    if (access(boost_path, F_OK) != 0) {
        #ifdef CONSOLE_DEBUG
        printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: \033[38;2;255;165;0mDynamic Boost interface not supported.\033[39m\n",debugtag());
        #endif
        return -2;
    }
    int fd = open(boost_path, O_WRONLY);
    if (fd < 0) {
        #ifdef CONSOLE_DEBUG
        printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: \033[38;2;255;105;97mFailed to access Dynamic Boost control.\033[39m\n",debugtag());
        #endif
        return -1;
    }
    ssize_t w = write(fd, value, 1);
    close(fd);
    if (w != 1) {
        #ifdef CONSOLE_DEBUG
        printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: \033[38;2;255;105;97mFailed to %s Intel Dynamic Boost.\033[39m\n",debugtag(), enable ? "enable" : "disable");
        #endif
        return -1;
    }
    #ifdef CONSOLE_DEBUG
    printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: Intel Dynamic Boost %s.\033[39m\n",debugtag(), enable ? "enabled" : "disabled");
    #endif
    return 0;
}




//******************************************************************************************************************************************************
//------------------------------------------------------------------------------------------------------------------------------ PROFILES FUNCTION
//------------------------------------------------------------------------------------------------------------------------------------------------------


static inline int set_energy_profile(profile_type_t profile, int ultra) {
    #ifdef CONSOLE_DEBUG
    const char *profile_names[] = {
        [PROFILE_LOW_POWER] = "Low Power",
        [PROFILE_ULTRA_LOW_POWER] = "Ultra Low Power",
        [PROFILE_BALANCED] = "Balanced",
        [PROFILE_PERFORMANCE] = "Performance"
    };
    printf("\033[37m%s  -----------------------------------------------------------\n",debugtag());
    printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: Setting \033[38;2;255;255;0m[%s]\033[38;2;144;238;144m Profile...\033[39m\n",debugtag(), profile_names[profile]);
    #endif

    int error_count = 0;
    const char *platform_profile, *gpu_profile, *pstate_epp, *pcie_aspm, *sata_power, *io_scheduler;
    int cpu_boost, usb_autosuspend, runtime_pm, wifi_power_save, bluetooth_power_save, sound_power_save, dirty_parameters, laptopmode;
    const char *preferred_governors[3];
    float max_freq_factor = 1.0;

    switch (profile) {
        case PROFILE_LOW_POWER:
        case PROFILE_ULTRA_LOW_POWER:
            platform_profile = "low-power";
            gpu_profile = "low_power";
            pstate_epp = "power";
            pcie_aspm = "powersupersave";
            sata_power = "min_power";
            io_scheduler = "low_power";
            cpu_boost = 0;
            usb_autosuspend = 1;
            runtime_pm = 1;
            wifi_power_save = 1;
            bluetooth_power_save = 1;
            sound_power_save = 1;
            dirty_parameters = 1;
            laptopmode = 1;
            preferred_governors[0] = "powersave";
            preferred_governors[1] = "ondemand";
            preferred_governors[2] = NULL;
            max_freq_factor = (ultra && profile == PROFILE_ULTRA_LOW_POWER) ? (eco_freq_cap_pct * 0.5f / 100.0f) : (eco_freq_cap_pct / 100.0f);
            break;

        case PROFILE_BALANCED:
            platform_profile = "balanced";
            gpu_profile = "balanced";
            pstate_epp = "balance_performance";
            pcie_aspm = "default";
            sata_power = "med_power_with_dipm";
            io_scheduler = "balanced";
            cpu_boost = 1;
            usb_autosuspend = balanced_usb_autosuspend_flag;
            runtime_pm = 1;
            wifi_power_save = 1;
            bluetooth_power_save = 1;
            sound_power_save = 1;
            dirty_parameters = 1;
            laptopmode = 1;
            preferred_governors[0] = "ondemand";
            preferred_governors[1] = "schedutil";
            preferred_governors[2] = "powersave";
            max_freq_factor = 1.0;
            break;

        case PROFILE_PERFORMANCE:
            platform_profile = "performance";
            gpu_profile = "performance";
            pstate_epp = "performance";
            pcie_aspm = "performance";
            sata_power = "max_performance";
            io_scheduler = "performance";
            cpu_boost = 1;
            usb_autosuspend = 0;
            runtime_pm = 0;
            wifi_power_save = 0;
            bluetooth_power_save = 0;
            sound_power_save = 0;
            dirty_parameters = 0;
            laptopmode = 0;
            preferred_governors[0] = "performance";
            preferred_governors[1] = "schedutil";
            preferred_governors[2] = NULL;
            max_freq_factor = 1.0;
            break;

        default:
            #ifdef CONSOLE_DEBUG
            printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: \033[38;2;255;105;97mInvalid profile specified.\033[39m\n",debugtag());
            #endif
            return -1;
    }

    set_platform_profile(platform_profile);
    em_set_gpu_power_profile(gpu_profile);

        int governor_set = 0;
        for (int i = 0; preferred_governors[i] != NULL; i++) {
            if (strstr(available_governors, preferred_governors[i])) {
                if (em_set_cpu_governor(preferred_governors[i]) == -1) {
                         error_count++; ///<*************** ici retry apres un detect_govs(); avant de marquer l'erreur
                       }

                #ifdef CONSOLE_DEBUG
                printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: CPU governor set to '%s'.\033[39m\n",debugtag(), preferred_governors[i]);
                #endif
                governor_set = 1;
                break;
            }
        }
        if (!governor_set) {
            #ifdef CONSOLE_DEBUG
            printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: \033[38;2;255;165;0mWarning: No suitable governor found for %s profile. CPU governor not set.\033[39m\n",debugtag(), profile_names[profile]);
            #endif
            error_count++;
        }


// EPP/EPB CPU
if (em_set_cpu_perf_policy(pstate_epp) != 0) {
    #ifdef CONSOLE_DEBUG
    printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: \033[38;2;255;105;97mFailed to apply CPU energy/performance policy '%s'.\033[39m\n",debugtag(), pstate_epp);
    #endif
    error_count++;
}




    unsigned long target_max_freq = (profile == PROFILE_PERFORMANCE) ? max_freq_khz_sys : (unsigned long)(max_freq_khz_sys * max_freq_factor);
    if (target_max_freq < min_freq_khz_sys) target_max_freq = min_freq_khz_sys;
unsigned long target_min_freq = 0;
if (profile == PROFILE_PERFORMANCE) {
    target_min_freq = target_max_freq / 2;
    if (target_min_freq < min_freq_khz_sys) {
        target_min_freq = min_freq_khz_sys;
    }
} else {
target_min_freq = min_freq_khz_sys;
}
    if (em_set_cpu_frequency_range(target_min_freq, target_max_freq) != 0) {
        #ifdef CONSOLE_DEBUG
        printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: \033[38;2;255;105;97mFailed to set CPU frequency range.\033[39m\n",debugtag());
        #endif
        error_count++;
    }
    #ifdef CONSOLE_DEBUG
    printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: CPU frequency range set to %lu KHz (min) - %lu KHz (max).\033[39m\n",debugtag(), target_min_freq, target_max_freq);
    #endif

    // Configuration du boost CPU
    if (em_set_cpu_boost(cpu_boost) == -1) {
        #ifdef CONSOLE_DEBUG
        printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: \033[38;2;255;105;97mWarning: Could not %s CPU boost for %s profile.\033[39m\n",debugtag(), cpu_boost ? "enable" : "disable", profile_names[profile]);
        #endif
        error_count++;
    }



if (cpu_type == CPU_TYPE_AMD) {
    if (profile == PROFILE_LOW_POWER || profile == PROFILE_ULTRA_LOW_POWER) {
        if (em_set_amd_cpu_perf_pct(40, 60) != 0) error_count++;
    } else if (profile == PROFILE_BALANCED) {
        if (em_set_amd_cpu_perf_pct(60, 100) != 0) error_count++;
    } else if (profile == PROFILE_PERFORMANCE) {
        if (em_set_amd_cpu_perf_pct(100, 100) != 0) error_count++;
    }
} else if (cpu_type == CPU_TYPE_INTEL) {
    if (profile == PROFILE_LOW_POWER || profile == PROFILE_ULTRA_LOW_POWER) {
        if (em_set_intel_cpu_perf_pct(20, 40) != 0) error_count++;
    } else if (profile == PROFILE_BALANCED) {
        if (em_set_intel_cpu_perf_pct(40, 100) != 0) error_count++;
    } else if (profile == PROFILE_PERFORMANCE) {
        if (em_set_intel_cpu_perf_pct(100, 100) != 0) error_count++;
    }
}


    // P-state EPP (intel)
    if (cpu_type == CPU_TYPE_INTEL && em_set_pstate_epp(pstate_epp) == -1) {
        #ifdef CONSOLE_DEBUG
        printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: \033[38;2;255;105;97mP-state EPP not available or failed to set to '%s'.\033[39m\n",debugtag(), pstate_epp);
        #endif
        error_count++;
    } else if (cpu_type != CPU_TYPE_INTEL) {
        #ifdef CONSOLE_DEBUG
        printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: \033[38;2;255;165;0mP-state EPP not applicable (non-Intel CPU).\033[39m\n",debugtag());
        #endif
    }

    //intel dyn boost
    if (cpu_type == CPU_TYPE_INTEL) {
        int dyn_boost_enable =
            (profile == PROFILE_LOW_POWER || profile == PROFILE_ULTRA_LOW_POWER) ? 0 : 1;
        if (em_set_cpu_dyn_boost(dyn_boost_enable) != 0) error_count++;
    }



    if (em_set_pcie_aspm(pcie_aspm) == -1) {
        #ifdef CONSOLE_DEBUG
        printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: \033[38;2;255;105;97mFailed to set PCIe ASPM to '%s'.\033[39m\n",debugtag(), pcie_aspm);
        #endif
        error_count++;
    }
    if (em_set_usb_autosuspend(usb_autosuspend) == -1) {
        #ifdef CONSOLE_DEBUG
        printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: \033[38;2;255;105;97mFailed to %s USB autosuspend.\033[39m\n",debugtag(), usb_autosuspend ? "enable" : "disable");
        #endif
        error_count++;
    }
    if (em_set_sata_link_power_management(sata_power) == -1) {
        #ifdef CONSOLE_DEBUG
        printf("\033[37m%s# \033[38;2;144;238;144m ¤¤ energy_manager: \033[38;2;255;105;97mWarning: Could not set SATA link power management.\033[39m\n",debugtag());
        #endif
        error_count++;
    }
    if (em_set_runtime_pm_for_all_devices(runtime_pm) == -1) {
        #ifdef CONSOLE_DEBUG
        printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: \033[38;2;255;105;97mWarning: Could not %s runtime PM for all devices.\033[39m\n",debugtag(), runtime_pm ? "enable" : "disable");
        #endif
        error_count++;
    }
    if (em_set_wifi_power_save(wifi_power_save) == -1) {
        #ifdef CONSOLE_DEBUG
        printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: \033[38;2;255;105;97mWarning: Could not set wifi power management.\033[39m\n",debugtag());
        #endif
        error_count++;
    }
    if (em_set_bluetooth_power_save(bluetooth_power_save) == -1) {
        #ifdef CONSOLE_DEBUG
        printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: \033[38;2;255;105;97mWarning: Could not set bluetooth power management.\033[39m\n",debugtag());
        #endif
        error_count++;
    }
    if (em_set_io_scheduler(io_scheduler) == -1) {
        #ifdef CONSOLE_DEBUG
        printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: \033[38;2;255;105;97mWarning: Could not set I/O scheduler for %s profile.\033[39m\n",debugtag(), profile_names[profile]);
        #endif
        error_count++;
    }
    if (em_set_dirty_parameters(dirty_parameters) == -1) {
        #ifdef CONSOLE_DEBUG
        printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: \033[38;2;255;105;97mWarning: Could not set dirty_parameters.\033[39m\n",debugtag());
        #endif
        error_count++;
    }
    if (em_set_laptopmode(laptopmode) == -1) {
        #ifdef CONSOLE_DEBUG
        printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: \033[38;2;255;105;97mWarning: Could not set laptopmode.\033[39m\n",debugtag());
        #endif
        error_count++;
    }
    if (em_set_sound_power_save(sound_power_save) == -1) {
        #ifdef CONSOLE_DEBUG
        printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: \033[38;2;255;105;97mWarning: Could not %s sound power management.\033[39m\n",debugtag(), sound_power_save ? "enable" : "disable");
        #endif
        error_count++;
    }

    #ifdef CONSOLE_DEBUG
    printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: %s configured with %d error(s).\033[39m\n",debugtag(), profile_names[profile], error_count);
    printf("\033[37m%s  -----------------------------------------------------------\n",debugtag());
    printf("\033[48;2;72;19;0m\033[37m%s\n",debugtag());
    #endif

    return error_count == 0 ? 0 : -1;
}
static int set_charge_limit(int value) {
    // Try common battery paths for charge_control_end_threshold
    const char *paths[] = {
        "/sys/class/power_supply/BAT0/charge_control_end_threshold",
        "/sys/class/power_supply/BAT1/charge_control_end_threshold",
        NULL
    };
    for (int i = 0; paths[i]; i++) {
        int fd = open(paths[i], O_WRONLY);
        if (fd >= 0) {
            char buf[8];
            int len = snprintf(buf, sizeof(buf), "%d", value);
            ssize_t w = write(fd, buf, len);
            close(fd);
            #ifdef CONSOLE_DEBUG
            printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: Charge limit set to %d%% via %s.\033[39m\n", debugtag(), value, paths[i]);
            #endif
            return (w == len) ? 0 : -1;
        }
    }
    #ifdef CONSOLE_DEBUG
    printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: \033[38;2;255;165;0mCharge limit not available on this hardware.\033[39m\n", debugtag());
    #endif
    return -1;
}








static inline int set_low_profile(int ultra) {
    return set_energy_profile(ultra ? PROFILE_ULTRA_LOW_POWER : PROFILE_LOW_POWER, ultra);
}
static inline int set_normal_profile() {
    return set_energy_profile(PROFILE_BALANCED, 0);
}
static inline int set_perf_profile() {
    return set_energy_profile(PROFILE_PERFORMANCE, 0);
}




static int get_cpu_online(int cpu) {
    char path[64];
    snprintf(path, sizeof(path), CPU_SYSFS_PATH "cpu%d/online", cpu);
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return 1;
    char val;
    ssize_t r = read(fd, &val, 1);
    close(fd);
    if (r != 1) return 1;
    return (val == '1') ? 1 : 0;
}




static void set_cpu_core_online(int cpu, int online) {
    if (cpu == 0) return;
    char path[64];
    snprintf(path, sizeof(path), CPU_SYSFS_PATH "cpu%d/online", cpu);
    int fd = open(path, O_WRONLY | O_CLOEXEC);
    if (fd < 0) return;
    char val[2] = { online ? '1' : '0', '\n' };
    write(fd, val, 2);
    close(fd);
}


void disable_cpu_cores(void) {
    sync();
    cpu_count = 0;
    DIR *cpudir = opendir(CPU_SYSFS_PATH);
    struct dirent *entry;
    int cpu;
    if (cpudir) {
        while ((entry = readdir(cpudir)) != NULL) {
            if (strncmp(entry->d_name, "cpu", 3) == 0 && my_isdigit(entry->d_name[3])) {
                cpu = atoi(entry->d_name + 3);
                if (cpu < MAX_CPUS) {
                    cpu_online_initial[cpu] = get_cpu_online(cpu);
                    cpu_count = (cpu > cpu_count) ? cpu : cpu_count;
                }
            }
        }
        closedir(cpudir);
    }
    cpu_count++;
#ifdef CONSOLE_DEBUG
printf("\033[37m%s\033[38;2;200;150;250m                     !---  deactivate all cores except 1\033[39m\n",debugtag()); ///only half the number of the cores to desactivate
#endif
    for (cpu = 0; cpu < cpu_count; ++cpu) {
        if (cpu != 0) { set_cpu_core_online(cpu, 0);
#ifdef CONSOLE_DEBUG
printf("\033[37m%s\033[38;2;200;150;250m                                 !---  deactivating core %d\033[39m\n",debugtag(),cpu);
#endif
         }
    }
#ifdef CONSOLE_DEBUG
printf("\033[48;2;72;19;0m\033[37m%s\n",debugtag());
printf("\033[37m%s \033[38;2;144;238;144m______________________________________________________________________________________\033[39m\n",debugtag());
printf("\033[48;2;72;19;0m\033[37m%s\n",debugtag());
#endif
}


void reenable_cpu_cores(void) {
#ifdef CONSOLE_DEBUG
printf("\033[37m%s\033[38;2;220;180;240m                      !---  reactivating cpu cores\033[39m\n",debugtag());
#endif
    int cpu;
    for (cpu = 1; cpu < cpu_count; ++cpu) {
        if (cpu_online_initial[cpu]) {
            #ifdef CONSOLE_DEBUG
            printf("\033[37m%s\033[38;2;220;180;240m                                 !---  activating core %d\033[39m\n",debugtag(),cpu);
            #endif
            set_cpu_core_online(cpu, 1);
        }
    }
#ifdef CONSOLE_DEBUG
printf("\033[48;2;72;19;0m\033[37m%s\n",debugtag());
#endif
}





static inline int set_cpu_driver_opmode(int battery_mode) {
    const char *intel_path = "/sys/devices/system/cpu/intel_pstate/status";
    const char *amd_path   = "/sys/devices/system/cpu/amd_pstate/status";
    const char *opmode = battery_mode ? "active" : "passive";
    if (access("/sys/devices/system/cpu/intel_pstate", F_OK) == 0) {
        int fd = open(intel_path, O_WRONLY);
        if (fd < 0) {
            #ifdef CONSOLE_DEBUG
            printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: \033[38;2;255;165;0mIntel P-state driver present, but unable to set operation mode '%s'.\033[39m\n",debugtag(), opmode);
            #endif
            return -1;
        }
        ssize_t w = write(fd, opmode, strlen(opmode));
        close(fd);
        #ifdef CONSOLE_DEBUG
        if (w == (ssize_t)strlen(opmode)) {
            printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: Intel P-state opmode set to '%s'.\033[39m\n",debugtag(), opmode);
        } else {
            printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: \033[38;2;255;105;97mFailed to write Intel P-state opmode '%s'.\033[39m\n",debugtag(), opmode);
        }
        #endif
        return (w == (ssize_t)strlen(opmode)) ? 0 : -1;
    } else if (access("/sys/devices/system/cpu/amd_pstate", F_OK) == 0) {
        int fd = open(amd_path, O_WRONLY);
        if (fd < 0) {
            #ifdef CONSOLE_DEBUG
            printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: \033[38;2;255;165;0mAMD P-state driver present, but unable to set operation mode '%s'.\033[39m\n",debugtag(), opmode);
            #endif
            return -1;
        }
        ssize_t w = write(fd, opmode, strlen(opmode));
        close(fd);
        #ifdef CONSOLE_DEBUG
        if (w == (ssize_t)strlen(opmode)) {
            printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: AMD P-state opmode set to '%s'.\033[39m\n",debugtag(), opmode);
        } else {
            printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: \033[38;2;255;105;97mFailed to write AMD P-state opmode '%s'.\033[39m\n",debugtag(), opmode);
        }
        #endif
        return (w == (ssize_t)strlen(opmode)) ? 0 : -1;
    }
    #ifdef CONSOLE_DEBUG
    printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: \033[38;2;255;165;0mNo known P-state driver detected, cannot apply operation mode.\033[39m\n",debugtag());
    #endif
    return -2;
}






int chmod_backlight_brightness(void) {
    glob_t glob_result;
    int ret;
    ret = glob("/sys/class/backlight/*/brightness", 0, NULL, &glob_result);
    if (ret != 0) {
        if (ret == GLOB_NOMATCH) {
            return -1;
        }
        return -2;
    }
    mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
    int result = 0;
    for (size_t i = 0; i < glob_result.gl_pathc; i++) {
        if (chmod(glob_result.gl_pathv[i], mode) != 0) {
            result = -2;
        }
    }
    globfree(&glob_result);
    return result;
}


int pong(void) {
    #ifdef CONSOLE_DEBUG
    printf("\033[37m%s \033[38;2;255;255;0m ping Received, answering 1 :-p (did you expect pong ?) \033[39m\n",debugtag());
    printf("\033[48;2;72;19;0m\033[37m%s\n",debugtag());
    #endif
return 1;
}



int set_rfkill_state(const char *type, int state) {
#ifdef CONSOLE_DEBUG
    printf("\033[37m%s\033[38;2;220;180;240m              !--->  set_rfkill_state (%s:%d)\033[39m\n", debugtag(), type,state);
    printf("\033[48;2;72;19;0m\033[37m%s\n", debugtag());
#endif
    if (!type) return -1;
    DIR *dir = opendir("/sys/class/rfkill");
    if (!dir) return -1;
    struct dirent *entry;
    char path[256], kind[32];
    int modified = 0;
    while ((entry = readdir(dir))) {
        if (strncmp(entry->d_name, "rfkill", 6) != 0)
            continue;
        int len = snprintf(path, sizeof(path), "/sys/class/rfkill/%s/type", entry->d_name);
        if (len < 0 || (size_t)len >= sizeof(path)) continue;
        int fd = open(path, O_RDONLY | O_CLOEXEC);
        if (fd < 0) continue;
        ssize_t r = read(fd, kind, sizeof(kind)-1);
        close(fd);
        if (r <= 0) continue;
        kind[r] = '\0';
        char *nl = strchr(kind, '\n');
        if (nl) *nl = '\0';
        if (strcmp(kind, type) == 0) {
            len = snprintf(path, sizeof(path), "/sys/class/rfkill/%s/soft", entry->d_name);
            if (len < 0 || (size_t)len >= sizeof(path)) continue;
            fd = open(path, O_WRONLY | O_CLOEXEC);
            if (fd < 0) continue;
            char val = state ? '0' : '1';
            char wbuf[2] = { val, '\n' };
            write(fd, wbuf, 2);
            close(fd);
            modified++;
        }
    }
    closedir(dir);
    return (modified > 0) ? 0 : -1;
}



gboolean init_eth_interface(void) {
    struct ifaddrs *ifaddr, *ifa;
    int sock = -1;
    const char *prefixes[] = { "eth", "en", "eno", "ens", "enp" };
    size_t nprefix = sizeof(prefixes) / sizeof(prefixes[0]);
    if (getifaddrs(&ifaddr) == -1)
        return FALSE;
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        freeifaddrs(ifaddr);
        return FALSE;
    }
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (!ifa->ifa_name)
            continue;
        gboolean matched = FALSE;
        for (size_t i = 0; i < nprefix; i++) {
            if (strncmp(ifa->ifa_name, prefixes[i], strlen(prefixes[i])) == 0) {
                matched = TRUE;
                break;
            }
        }
        if (!matched)
            continue;
        struct ifreq ifr = {0};
        strncpy(ifr.ifr_name, ifa->ifa_name, IFNAMSIZ - 1);
        ifr.ifr_name[IFNAMSIZ - 1] = '\0';
        if (ioctl(sock, SIOCGIFHWADDR, &ifr) == 0) {
            if (ifr.ifr_hwaddr.sa_family == ARPHRD_ETHER) {
                strncpy(iface_name, ifa->ifa_name, IFNAMSIZ - 1);
                iface_name[IFNAMSIZ - 1] = '\0';
                break;
            }
        }
    }
    close(sock);
    freeifaddrs(ifaddr);
    return iface_name[0] != '\0';
}



void manage_interface(gboolean up) {
    if (iface_name[0] == '\0') {
         #ifdef CONSOLE_DEBUG
          printf("\033[37m%s \033[38;2;255;105;97m    !--- Cannot toggle Ethernet device (not detected).\033[39m\n",debugtag());
          printf("\033[48;2;72;19;0m\033[37m%s\n",debugtag());
         #endif
        return;
        }
    {
        struct ifreq ifr = {0};
        int fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (fd >= 0) {
            strncpy(ifr.ifr_name, iface_name, IFNAMSIZ - 1);
            ifr.ifr_name[IFNAMSIZ - 1] = '\0';
            if (ioctl(fd, SIOCGIFFLAGS, &ifr) >= 0) {
                if (up)
                    ifr.ifr_flags |= IFF_UP;
                else
                    ifr.ifr_flags &= ~IFF_UP;
                ioctl(fd, SIOCSIFFLAGS, &ifr);
            }
            close(fd);
        }
    }
    {
        pid_t pid = fork();
        if (pid == 0) {
            int devnull = open("/dev/null", O_WRONLY);
            if (devnull >= 0) {
                dup2(devnull, STDOUT_FILENO);
                dup2(devnull, STDERR_FILENO);
                close(devnull);
            }
            char *argv[] = {
                "/usr/bin/nmcli",
                "device",
                "set",
                iface_name,
                "managed",
                up ? "yes" : "no",
                NULL
            };
            execv("/usr/bin/nmcli", argv);
            _exit(127);
        } else if (pid > 0) {
            int status;
            waitpid(pid, &status, 0);
        }
    }
     #ifdef CONSOLE_DEBUG
     printf("\033[48;2;72;19;0m\033[37m%s\n",debugtag());
     printf("\033[37m%s \033[38;2;144;238;144m        !--- Ethernet device (%s) %s\033[39m\n",debugtag(), iface_name, up ? "enabled" : "disabled");
     printf("\033[48;2;72;19;0m\033[37m%s\n",debugtag());
     #endif
}




void disable_wol_on_iface(void) {
    if (iface_name[0] == '\0') {
    #ifdef CONSOLE_DEBUG
        printf("\033[37m%s \033[38;2;255;105;97m    !--- Cannot disable WoL (device not detected).\033[39m\n", debugtag());
    #endif
        return;
    }
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
    #ifdef CONSOLE_DEBUG
        perror("socket");
        printf("\033[37m%s \033[38;2;255;105;97m    !--- Cannot create socket for WoL control.\033[39m\n", debugtag());
    #endif
        return;
    }
    struct ifreq ifr = {0};
    struct ethtool_wolinfo wol = {0};
    strncpy(ifr.ifr_name, iface_name, IFNAMSIZ - 1);
    ifr.ifr_name[IFNAMSIZ - 1] = '\0';
    wol.cmd = ETHTOOL_GWOL;
    ifr.ifr_data = (char *)&wol;
    if (ioctl(sockfd, SIOCETHTOOL, &ifr) < 0) {
    #ifdef CONSOLE_DEBUG
        perror("ioctl get wol");
        printf("\033[37m%s \033[38;2;255;105;97m    !--- ioctl get wol failed.\033[39m\n", debugtag());
    #endif
        close(sockfd);
        return;
    }
    wol.wolopts = 0;
    wol.cmd = ETHTOOL_SWOL;
    ifr.ifr_data = (char *)&wol;
    if (ioctl(sockfd, SIOCETHTOOL, &ifr) < 0) {
    #ifdef CONSOLE_DEBUG
        perror("ioctl set wol");
        printf("\033[37m%s \033[38;2;255;105;97m    !--- ioctl set wol failed.\033[39m\n", debugtag());
    #endif
        close(sockfd);
        return;
    }
    close(sockfd);
#ifdef CONSOLE_DEBUG
    printf("\033[37m%s \033[38;2;144;238;144m        !--- Wake On LAN disabled on %s\033[39m\n", debugtag(), iface_name);
    printf("\033[48;2;72;19;0m\033[37m%s\n", debugtag());
#endif
}

//=============================================================================================================






void add_to_frozen_list(const char* service_name) {
    if (frozen_count >= MAX_FREEZABLE) {
        #ifdef CONSOLE_DEBUG
        printf("\033[37m%s \033[38;2;255;165;0m ¤¤ services_manager: \033[38;2;255;105;97mFrozen list full, cannot add %s\033[39m\n", debugtag(), service_name);
        #endif
        return;
    }
    for (int i = 0; i < frozen_count; i++) {
        if (strcmp(frozen_services[i], service_name) == 0) {
            return;
        }
    }
    strncpy(frozen_services[frozen_count], service_name, sizeof(frozen_services[frozen_count]) - 1);
    frozen_services[frozen_count][sizeof(frozen_services[frozen_count]) - 1] = '\0';
    frozen_count++;
    #ifdef CONSOLE_DEBUG
    printf("\033[37m%s \033[38;2;255;165;0m ¤¤ services_manager: \033[38;2;173;216;230m                |-- Freezing %s\033[39m\n", debugtag(), service_name);
    #endif
}






void merge_whitelists() {
    merged_whitelist_count = 0;
    for (int i = 0; i < hardcoded_whitelist_size && merged_whitelist_count < MAX_FREEZABLE; i++) {
        strncpy(merged_whitelist[merged_whitelist_count], hardcoded_whitelist[i], sizeof(merged_whitelist[merged_whitelist_count]) - 1);
        merged_whitelist[merged_whitelist_count][sizeof(merged_whitelist[merged_whitelist_count]) - 1] = '\0';
        merged_whitelist_count++;
    }
    for (int i = 0; i < whitelist_size && merged_whitelist_count < MAX_FREEZABLE; i++) {
        int already_exists = 0;
        for (int j = 0; j < merged_whitelist_count; j++) {
            if (strcmp(merged_whitelist[j], whitelist[i]) == 0) {
                already_exists = 1;
                break;
            }
        }
        if (!already_exists) {
            strncpy(merged_whitelist[merged_whitelist_count], whitelist[i], sizeof(merged_whitelist[merged_whitelist_count]) - 1);
            merged_whitelist[merged_whitelist_count][sizeof(merged_whitelist[merged_whitelist_count]) - 1] = '\0';
            merged_whitelist_count++;
        }
    }
}



int is_whitelisted(const char *svc) {
    for (int i = 0; i < merged_whitelist_count; i++)
        if (strcmp(merged_whitelist[i], svc) == 0)
            return 1;
    return 0;
}



void freeze_services_dbus() {
    if (frozen_count == 0) { 
    scan_service_activity();
    add_blacklist_to_freezables();
    sd_bus *bus = NULL;
    sd_bus_message *msg = NULL;
    sd_bus_error error = SD_BUS_ERROR_NULL;
    int r = sd_bus_open_system(&bus);
    if (r < 0) {
        #ifdef CONSOLE_DEBUG
        printf("\033[37m%s \033[38;2;255;165;0m ¤¤ services_manager: \033[38;2;255;105;97msd_bus_open_system failed: %d\033[39m\n", debugtag(), r);
        #endif
        return;
    }
    for (int i = 0; i < freezable_count; i++) {
        char unit_name[128];
        snprintf(unit_name, sizeof(unit_name), "%s.service", freezable_services[i]);
        int r = sd_bus_call_method(bus,
            "org.freedesktop.systemd1", "/org/freedesktop/systemd1",
            "org.freedesktop.systemd1.Manager", "StopUnit",
            &error, &msg, "ss", unit_name, "replace");
        if (r >= 0) {
            add_to_frozen_list(freezable_services[i]);
        } else {
            #ifdef CONSOLE_DEBUG
            printf("\033[37m%s \033[38;2;255;165;0m ¤¤ services_manager: \033[38;2;255;105;97mFailed to freeze %s: %s\033[39m\n", 
                   debugtag(), freezable_services[i], error.message ?: "unknown");
            #endif
        }
        if (msg) {
            sd_bus_message_unref(msg);
            msg = NULL;
        }
        sd_bus_error_free(&error);
    }
    sd_bus_unref(bus);
 } else {
             #ifdef CONSOLE_DEBUG
            printf("\033[37m%s \033[38;2;255;165;0m ¤¤ services_manager: \033[38;2;255;105;97mServices already freezed.\033[39m\n",debugtag());
            #endif
           }
}



float get_cpu_total(pid_t pid) {
    char path[32] = "/proc/";
    int i = 6;
    pid_t p = pid;
    if (p == 0) return -1.0f;
    char pid_buf[12];
    int pid_len = 0;
    while (p && pid_len < (int)sizeof(pid_buf)) {
        pid_buf[pid_len++] = '0' + (p % 10);
        p /= 10;
    }
    if (pid_len == 0) pid_buf[pid_len++] = '0';
    while (pid_len > 0 && i < (int)sizeof(path) - 6) {
        path[i++] = pid_buf[--pid_len];
    }
    const char *suffix = "/stat";
    for (int j = 0; suffix[j] && i < (int)sizeof(path) - 1; j++) {
        path[i++] = suffix[j];
    }
    path[i] = '\0';
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1.0f;
    char buf[512];
    ssize_t r = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (r <= 0) return -1.0f;
    buf[r] = '\0';
    char *ptr = strchr(buf, ')');
    if (!ptr) return -1.0f;
    ptr++;
    while (*ptr == ' ') ptr++;
    char *next = strchr(ptr, ' ');
    if (!next) return -1.0f;
    ptr = next +1;
    next = strchr(ptr, ' ');
    if (!next) return -1.0f;
    ptr = next + 1;
    for (int skip=0; skip<9; skip++) {
        next = strchr(ptr, ' ');
        if (!next) return -1.0f;
        ptr = next + 1;
    }
    unsigned long utime = strtoul(ptr, &next, 10);
    if (ptr == next) return -1.0f;
    unsigned long stime = strtoul(next, NULL, 10);
    long clk = sysconf(_SC_CLK_TCK);
    if (clk <= 0) return -1.0f;
    return (utime + stime) / (float)clk;
}




unsigned long get_mem_rss_kb(pid_t pid) {
    char path[32] = "/proc/";
    int i = 6;
    pid_t p = pid;
    if (p == 0) return ULONG_MAX;
    char pid_buf[12];
    int pid_len = 0;
    while (p && pid_len < (int)sizeof(pid_buf)) {
        pid_buf[pid_len++] = '0' + (p % 10);
        p /= 10;
    }
    if (pid_len == 0) pid_buf[pid_len++] = '0';
    while (pid_len > 0 && i < (int)sizeof(path) - 8) {
        path[i++] = pid_buf[--pid_len];
    }
    const char *suffix = "/status";
    for (int j = 0; suffix[j] && i < (int)sizeof(path) - 1; j++) {
        path[i++] = suffix[j];
    }
    path[i] = '\0';
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return ULONG_MAX;
    char buf[128], *line_start, *line_end;
    ssize_t r;
    unsigned long rss = ULONG_MAX;
    int pos = 0;
    int buf_len = 0;
    while ((r = read(fd, buf + buf_len, sizeof(buf) - buf_len - 1)) > 0) {
        buf_len += r;
        buf[buf_len] = 0;
        while ((line_end = strchr(buf + pos, '\n')) != NULL) {
            *line_end = '\0';
            line_start = buf + pos;
            if (strncmp(line_start, "VmRSS:", 6) == 0) {
                char *pnum = line_start + 6;
                while (*pnum == ' ' || *pnum == '\t') pnum++;
                rss = strtoul(pnum, NULL, 10);
                close(fd);
                return rss;
            }
            pos = (line_end - buf) + 1;
        }
        if (pos > 0 && pos < buf_len) {
            memmove(buf, buf + pos, buf_len - pos);
            buf_len -= pos;
            pos = 0;
        } else if (pos == buf_len) {
            buf_len = 0;
            pos = 0;
        }
    }
    close(fd);
    return rss;
}



void scan_service_activity() {
        #ifdef CONSOLE_DEBUG
        printf("\033[37m%s \033[38;2;255;165;0m ¤¤ services_manager: \033[38;2;173;216;230m|-- Scanning low activity services...\033[39m\n",debugtag());
        #endif
    freezable_count = 0;
    sample_count = 0;
    sd_bus *bus = NULL;
    sd_bus_message *msg = NULL;
    sd_bus_error error = SD_BUS_ERROR_NULL;
    int r;
    r = sd_bus_open_system(&bus);
    if (r < 0) {
        #ifdef CONSOLE_DEBUG
        printf("\033[37m%s \033[38;2;255;165;0m ¤¤ services_manager: \033[38;2;255;105;97msd_bus_open_system failed: %d\033[39m\n",debugtag(), r);
        #endif
        return;
    }
    r = sd_bus_call_method(bus,
        "org.freedesktop.systemd1", "/org/freedesktop/systemd1",
        "org.freedesktop.systemd1.Manager", "ListUnits",
        &error, &msg, NULL);
    if (r < 0) {
        #ifdef CONSOLE_DEBUG
        printf("\033[37m%s \033[38;2;255;165;0m ¤¤ services_manager: \033[38;2;255;105;97mError listUnits: %s (%d)\033[39m\n",debugtag(), error.message ?: "unknown", r);
        #endif
        sd_bus_error_free(&error);
        sd_bus_unref(bus);
        return;
    }
    r = sd_bus_message_enter_container(msg, SD_BUS_TYPE_ARRAY, "(ssssssouso)");
    if (r < 0) {
        #ifdef CONSOLE_DEBUG
        printf("\033[37m%s \033[38;2;255;165;0m ¤¤ services_manager: \033[38;2;255;105;97mError enter_container ARRAY: %d\033[39m\n",debugtag(), r);
        #endif
        goto cleanup1;
    }
    while ((r = sd_bus_message_enter_container(msg, SD_BUS_TYPE_STRUCT, "ssssssouso")) > 0) {
        const char *name, *desc, *load, *active, *sub, *followed, *unit_path, *job_type, *job_path;
        uint32_t job_id;
        sd_bus_message_read(msg, "ssssssouso", &name, &desc, &load, &active, &sub, &followed, &unit_path, &job_id, &job_type, &job_path);
        sd_bus_message_exit_container(msg);
        if (!strstr(name, ".service")) continue;
        char svc[128];
        size_t L = strlen(name) - strlen(".service");
        strncpy(svc, name, L); svc[L] = '\0';
        if (is_whitelisted(svc)) continue;
        if (strcmp(active, "active") != 0) continue;
        sd_bus_message *prop = NULL;
        sd_bus_error_free(&error);
        r = sd_bus_call_method(bus,
            "org.freedesktop.systemd1", unit_path,
            "org.freedesktop.DBus.Properties", "Get",
            &error, &prop,
            "ss",
            "org.freedesktop.systemd1.Service",
            "MainPID");
        if (r < 0) {
            sd_bus_error_free(&error);
            continue;
        }
        uint32_t pid = 0;
        sd_bus_message_enter_container(prop, SD_BUS_TYPE_VARIANT, "u");
        sd_bus_message_read(prop, "u", &pid);
        sd_bus_message_exit_container(prop);
        sd_bus_message_unref(prop);
        if (pid == 0) continue;
        float total = get_cpu_total(pid);
        if (total < 0) continue;
        strncpy(samples[sample_count].name, svc, sizeof(svc));
        samples[sample_count].pid      = pid;
        samples[sample_count].last_cpu = total;
        sample_count++;
        // printf("sampled %s (PID %u) CPU=%.2f\n", svc, pid, total);
        if (sample_count >= MAX_FREEZABLE) break;
    }
    sd_bus_message_exit_container(msg);
    cleanup1:
        sd_bus_message_unref(msg);
        sd_bus_unref(bus);
    sleep(SAMPLE_INTERVAL_SEC);
    for (int i = 0; i < sample_count; i++) {
        char *svc = samples[i].name;
        uint32_t pid = samples[i].pid;
        float new_total = get_cpu_total(pid);
        if (new_total < 0) continue;
        float delta = new_total - samples[i].last_cpu;
        float pct   = delta * 100.0f / SAMPLE_INTERVAL_SEC;
        unsigned long rss = get_mem_rss_kb(pid);
       // printf("%s PID=%u ΔCPU=%.2fsec → %.2f%% RSS=%lukB\n", svc, pid, delta, pct, rss);
        if (pct < CPU_PCT_THRESHOLD && rss < MEM_THRESHOLD_KB) {
            strncpy(freezable_services[freezable_count], svc, sizeof(samples[i].name));
        #ifdef CONSOLE_DEBUG
        printf("\033[37m%s \033[38;2;255;165;0m ¤¤ services_manager:   |-- %s (PID %u, CPU=%.2f%%, RSS=%lukB)\033[39m\n",debugtag(), svc, pid, pct, rss);
        #endif
            freezable_count++;
        }
    }
#ifdef CONSOLE_DEBUG
printf("\033[37m%s \033[38;2;255;165;0m ¤¤ services_manager: \033[38;2;173;216;230m      |-- Freezables(\033[38;2;255;255;0m%d\033[38;2;173;216;230m):\033[39m\n",debugtag(),freezable_count);
if (freezable_count == 0)
printf("\033[37m%s \033[38;2;255;165;0m ¤¤ services_manager: \033[38;2;255;105;97mNo electable service for freezing.\033[39m\n",debugtag());
for (int i = 0; i < freezable_count; i++)
printf("\033[37m%s \033[38;2;255;165;0m ¤¤ services_manager:             |-- %s\033[39m\n",debugtag(), freezable_services[i]);
printf("\033[37m%s \033[38;2;255;165;0m ¤¤ services_manager: \033[38;2;173;216;230m      |-- Always Freeze(\033[38;2;255;255;0m%d\033[38;2;173;216;230m):\033[39m\n",debugtag(),blacklist_size);
if (blacklist_size == 0)
printf("\033[37m%s \033[38;2;255;165;0m ¤¤ services_manager: \033[38;2;255;105;97mNo service in blacklist.\033[39m\n",debugtag());
for (int i = 0; i < blacklist_size; i++)
printf("\033[37m%s \033[38;2;255;165;0m ¤¤ services_manager:             |-- %s\033[39m\n",debugtag(), blacklist[i]);
#endif
}



void add_blacklist_to_freezables() {
    for (int i = 0; i < blacklist_size && freezable_count < MAX_FREEZABLE; i++) {
        int already_exists = 0;
        for (int j = 0; j < freezable_count; j++) {
            if (strcmp(freezable_services[j], blacklist[i]) == 0) {
                already_exists = 1;
                break;
            }
        }
        if (!already_exists) {
            strncpy(freezable_services[freezable_count], blacklist[i], sizeof(freezable_services[freezable_count]) - 1);
            freezable_services[freezable_count][sizeof(freezable_services[freezable_count]) - 1] = '\0';
            freezable_count++;
        }
    }
}




void unfreeze_all_frozen_services() {
    if (frozen_count == 0) {
        #ifdef CONSOLE_DEBUG
        printf("\033[37m%s \033[38;2;255;165;0m ¤¤ services_manager: \033[38;2;255;255;0mNo frozen services to unfreeze\033[39m\n", debugtag());
        #endif
        return;
    }
    sd_bus *bus = NULL;
    sd_bus_message *msg = NULL;
    sd_bus_error error = SD_BUS_ERROR_NULL;
    int r = sd_bus_open_system(&bus);
    if (r < 0) {
        #ifdef CONSOLE_DEBUG
        printf("\033[37m%s \033[38;2;255;165;0m ¤¤ services_manager: \033[38;2;255;105;97msd_bus_open_system failed: %d. Cannot unfreeze services.\033[39m\n", debugtag(), r);
        #endif
        return;
    }
    for (int i = 0; i < frozen_count; i++) {
        char unit_name[128];
        snprintf(unit_name, sizeof(unit_name), "%s.service", frozen_services[i]);
        int r = sd_bus_call_method(bus,
            "org.freedesktop.systemd1", "/org/freedesktop/systemd1",
            "org.freedesktop.systemd1.Manager", "StartUnit",
            &error, &msg, "ss", unit_name, "replace");
        if (r >= 0) {
            #ifdef CONSOLE_DEBUG
             printf("\033[37m%s \033[38;2;255;165;0m ¤¤ services_manager: \033[38;2;255;182;193m  |-- Unfreezing %s\033[39m\n", debugtag(), frozen_services[i]);
             #endif
        } else {
            #ifdef CONSOLE_DEBUG
            printf("\033[37m%s \033[38;2;255;165;0m ¤¤ services_manager: \033[38;2;255;105;97mFailed to unfreeze %s: %s\033[39m\n", debugtag(), frozen_services[i], error.message ?: "unknown");
            #endif
        }
        if (msg) {
            sd_bus_message_unref(msg);
            msg = NULL;
        }
        sd_bus_error_free(&error);
    }
    sd_bus_unref(bus);
     frozen_count = 0;
}


void detect_govs() {
 if (em_read_sysfs("/sys/devices/system/cpu/cpufreq/policy0/scaling_available_governors", available_governors, sizeof(available_governors)) != 0) {
        #ifdef CONSOLE_DEBUG
        printf("\033[37m%s \033[38;2;144;238;144m \033[38;2;255;105;97mError reading available governors from policy0. Profile setup might be incomplete.\033[39m\n",debugtag());
                printf("\033[48;2;72;19;0m\033[37m%s\n",debugtag());
        #endif
    }
            #ifdef CONSOLE_DEBUG
            else { printf("\033[37m%s \033[38;2;144;238;144m ►Detected available governors:\033[39m\n",debugtag());
                      printf("\033[37m%s \033[38;2;144;238;144m   [%s]\033[39m\n",debugtag(),available_governors);
                     printf("\033[48;2;72;19;0m\033[37m%s\n",debugtag()); }
            #endif
}

#pragma GCC push_options
#pragma GCC optimize ("O3")
void handle_command(const char *cmdline, int client_sock) {
    char cmd[64], arg[256];
    cmd[0] = arg[0] = '\0';
    const char *colon = strchr(cmdline, ':');
    if (colon) {
        size_t cmd_len = colon - cmdline;
        if (cmd_len >= sizeof(cmd)) cmd_len = sizeof(cmd) - 1;
        strncpy(cmd, cmdline, cmd_len);
        cmd[cmd_len] = '\0';
        strncpy(arg, colon + 1, sizeof(arg) - 1);
        arg[sizeof(arg) - 1] = '\0';
    } else {
        strncpy(cmd, cmdline, sizeof(cmd) - 1);
        cmd[sizeof(cmd) - 1] = '\0';
    }
    if (strlen(cmd) == 0) return;

if (strcmp(cmd, "set_perf_profile") == 0) {
        set_perf_profile();
        dprintf(client_sock, "0\n");
    }
else if (strcmp(cmd, "set_normal_profile") == 0) {
        set_normal_profile();
        dprintf(client_sock, "0\n");
    }
else if (strcmp(cmd, "set_low_profile") == 0) {
    gboolean val = (atoi(arg) != 0);
    set_low_profile(val);
    dprintf(client_sock, "0\n");
}
else if (strcmp(cmd, "set_cpu_driver_opmode") == 0) {
    gboolean val = (atoi(arg) != 0);
    set_cpu_driver_opmode(val);
    detect_govs();
    dprintf(client_sock, "0\n");
}
else if (strcmp(cmd, "toggle_ethernet") == 0) {
    gboolean val = (atoi(arg) != 0);
    manage_interface(val);
    dprintf(client_sock, "0\n");
}
else if (strcmp(cmd, "disable_cpu_cores") == 0) {
        disable_cpu_cores();
        dprintf(client_sock, "0\n");
    }
else if (strcmp(cmd, "reenable_cpu_cores") == 0) {
       reenable_cpu_cores();
       dprintf(client_sock, "0\n");
    }
else if (strcmp(cmd, "set_whitelist") == 0) {
    set_whitelist_from_string(arg);
    dprintf(client_sock, "0\n");
}
else if (strcmp(cmd, "set_blacklist") == 0) {
    set_blacklist_from_string(arg);
    dprintf(client_sock, "0\n");
}
else if (strcmp(cmd, "freeze_services_dbus") == 0) {
        freeze_services_dbus();
        dprintf(client_sock, "0\n");
    }
else if (strcmp(cmd, "unfreeze_all_frozen_services") == 0) {
        unfreeze_all_frozen_services();
        dprintf(client_sock, "0\n");
    }
else if (strcmp(cmd, "power_off_now") == 0) {
        reboot_or_poweroff=TRUE;
        #ifdef CONSOLE_DEBUG
        printf("\033[37m%s \033[38;2;144;238;144m ►power off requested, will not perform unexpected termination actions.\033[39m\n",debugtag());
        printf("\033[48;2;72;19;0m\033[37m%s\n",debugtag());
        #endif
        dprintf(client_sock, "0\n");
    }
// else if (strcmp(cmd, "get_rfkill_state") == 0) {
//     int state = get_rfkill_state(arg);
//     dprintf(client_sock, "%d\n", state);
// }
else if (strcmp(cmd, "set_rfkill_state") == 0) {
    char rfk_type[64];
    int rfk_state = 0;
    sscanf(arg, "%63[^:]:%d", rfk_type, &rfk_state);
    set_rfkill_state(rfk_type, rfk_state);
    dprintf(client_sock, "0\n");
}
else if (strcmp(cmd, "set_webcam_power") == 0) {
    gboolean val = (atoi(arg) != 0);
    em_set_webcam_power(val);
    dprintf(client_sock, "%d\n", val);
}
else if (strcmp(cmd, "set_procs_whitelist") == 0) {
    set_procs_whitelist_from_string(arg);
    dprintf(client_sock, "0\n");
}
else if (strcmp(cmd, "set_procs_blacklist") == 0) {
    set_procs_blacklist_from_string(arg);
    dprintf(client_sock, "0\n");
}
else if (strcmp(cmd, "freeze_processes") == 0) {
    gboolean val = (atoi(arg) != 0);
    freeze_processes(val);
    dprintf(client_sock, "%d\n", val);
}
#ifdef CONSOLE_DEBUG
else if (strcmp(cmd, "waking_up") == 0) {
printf("\033[37m%s  ▬▬▬▬▬▬▬▬▬▬▬▬▬▬{ wake_up received }\n",debugtag());
}
#endif
else if (strcmp(cmd, "ping") == 0) {
    int result = pong();
    dprintf(client_sock, "%d\n", result);
}
else if (strcmp(cmd, "set_charge_limit") == 0) {
    int val = atoi(arg);
    if (val >= 60 && val <= 100) {
        set_charge_limit(val);
        dprintf(client_sock, "0\n");
    } else {
        dprintf(client_sock, "-1\n");
    }
}
else if (strcmp(cmd, "set_eco_freq_cap") == 0) {
    int val = atoi(arg);
    if (val >= 20 && val <= 80) {
        eco_freq_cap_pct = val;
        #ifdef CONSOLE_DEBUG
        printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: Eco freq cap set to %d%%.\033[39m\n", debugtag(), val);
        #endif
        dprintf(client_sock, "0\n");
    } else {
        dprintf(client_sock, "-1\n");
    }
}
else if (strcmp(cmd, "set_balanced_usb_autosuspend") == 0) {
    int val = atoi(arg);
    balanced_usb_autosuspend_flag = (val != 0) ? 1 : 0;
    #ifdef CONSOLE_DEBUG
    printf("\033[37m%s \033[38;2;144;238;144m ¤¤ energy_manager: Balanced USB autosuspend set to %d.\033[39m\n", debugtag(), balanced_usb_autosuspend_flag);
    #endif
    dprintf(client_sock, "0\n");
}
    else {
        dprintf(client_sock, "-1\n");
    }
}
#pragma GCC pop_options



void signal_handler(int sig) {
    (void)sig;
    keep_running = 0;
}


void disable_nmi_watchdog(void) {
    FILE *f = fopen("/proc/sys/kernel/nmi_watchdog", "w");
    if (!f) {
    #ifdef CONSOLE_DEBUG
        printf("\033[37m%s \033[38;2;255;105;97m    !--- can't open /proc/sys/kernel/nmi_watchdog.\033[39m\n", debugtag());
    #endif
        return;
    }
    if (fprintf(f, "0\n") < 0) {
    #ifdef CONSOLE_DEBUG
        printf("\033[37m%s \033[38;2;255;105;97m    !--- can't write in /proc/sys/kernel/nmi_watchdog.\033[39m\n", debugtag());
    #endif
        fclose(f);
        return;
    }
    fclose(f);
#ifdef CONSOLE_DEBUG
    printf("\033[37m%s \033[38;2;144;238;144m ►NMI watchdog disabled.\033[39m\n", debugtag());
    printf("\033[48;2;72;19;0m\033[37m%s\n", debugtag());
#endif
}




void am_i_root() {
    if (geteuid() != 0) {
#ifdef CONSOLE_DEBUG
        printf("\033[37m%s \033[38;2;144;238;144m \033[38;2;255;105;97m              Warning: yabatmand daemon needs root privileges.\033[39m\n",debugtag());
        printf("\033[48;2;72;19;0m\033[37m%s\n",debugtag());
        printf("\033[37m%s \033[38;2;144;238;144m \033[38;2;255;105;97m                           -[ exit program ]-\033[39m\n",debugtag());
        printf("\033[48;2;72;19;0m\033[37m%s\n",debugtag());
#endif
        exit(EXIT_FAILURE);
    }
#ifdef CONSOLE_DEBUG
            printf("\033[37m%s \033[38;2;144;238;144m ►Root privileges: ok.\033[39m\n",debugtag());
        printf("\033[48;2;72;19;0m\033[37m%s\n",debugtag());
#endif
}

int main(void) {
    #ifdef CONSOLE_DEBUG
    printf("\033[48;2;72;19;0m");
    printf("\n\n%s\033[38;2;144;238;144m       ------------------------------------------------------------------\033[39m\n",debugtag());
    printf("\033[37m%s\033[38;2;144;238;144m       |    Starting YaBatmand ~ energy daemon  [-- DEBUG BUILD --]     |\033[39m\n",debugtag());
    printf("\033[37m%s\033[38;2;144;238;144m       |            * not for normal use !! debug only !! *             |\033[39m\n",debugtag());
    printf("\033[37m%s\033[38;2;144;238;144m       ------------------------------------------------------------------\033[39m\n",debugtag());
    printf("\033[48;2;72;19;0m\033[37m%s\n",debugtag());
    #endif
am_i_root();
signal(SIGPIPE, SIG_IGN);
signal(SIGINT, signal_handler);
signal(SIGTERM, signal_handler);
int result = chmod_backlight_brightness();
    #ifdef CONSOLE_DEBUG
    if (result == 0) {
        printf("\033[37m%s \033[38;2;144;238;144m ►Backlight permissions successfully applied.\033[39m\n",debugtag());
        printf("\033[48;2;72;19;0m\033[37m%s\n",debugtag());
    } else if (result == -1) {
        printf("\033[37m%s \033[38;2;144;238;144m \033[38;2;255;105;97mWarning: no brightness control file found.\033[39m\n",debugtag());
        printf("\033[48;2;72;19;0m\033[37m%s\n",debugtag());
    } else {
        printf("\033[37m%s \033[38;2;144;238;144m \033[38;2;255;105;97mWarning: error applying  backlight permissions.\033[39m\n",debugtag());
        printf("\033[48;2;72;19;0m\033[37m%s\n",debugtag());
    }
    #endif
cpu_type = em_detect_cpu_type();
    #ifdef CONSOLE_DEBUG
    printf("\033[37m%s \033[38;2;144;238;144m ►Detected CPU type: %s\033[39m\n",debugtag(), cpu_type == CPU_TYPE_INTEL ? "Intel" : cpu_type == CPU_TYPE_AMD ? "AMD" : "Unknown");
    printf("\033[48;2;72;19;0m\033[37m%s\n",debugtag());
    #endif
    if (em_get_max_available_freq("/sys/devices/system/cpu/cpufreq/policy0", &max_freq_khz_sys) != 0) {
        #ifdef CONSOLE_DEBUG
        printf("\033[37m%s \033[38;2;144;238;144m \033[38;2;255;105;97mError determining max CPU freq for policy0. Using fallback (2GHz).\033[39m\n",debugtag());
        #endif
        max_freq_khz_sys = 2000000;
    }
    if (em_read_sysfs_ul("/sys/devices/system/cpu/cpufreq/policy0/cpuinfo_min_freq", &min_freq_khz_sys) != 0) {
        #ifdef CONSOLE_DEBUG
        printf("\033[37m%s \033[38;2;144;238;144m \033[38;2;255;105;97mError reading min CPU freq from policy0. Using fallback (400MHz).\033[39m\n",debugtag());
        #endif
        min_freq_khz_sys = 400000;
    }

detect_govs();

 if (!init_eth_interface()) {
        #ifdef CONSOLE_DEBUG
        printf("\033[37m%s \033[38;2;255;105;97mWarning: No ethernet device detected.\033[39m\n",debugtag());
        printf("\033[48;2;72;19;0m\033[37m%s\n",debugtag());
        #endif
 } else {
        #ifdef CONSOLE_DEBUG
        printf("\033[37m%s \033[38;2;144;238;144m ►Detected ethernet device:%s.\033[39m\n",debugtag(),iface_name);
        #endif
        disable_wol_on_iface();
 }
disable_nmi_watchdog();
em_detect_webcams();

 /* Pre-compile process freeze regexes (once at startup) */
 if (regcomp(&rx_msg_cached, PATTERN_MSG, REG_EXTENDED | REG_ICASE) == 0 &&
     regcomp(&rx_sync_cached, PATTERN_SYNC, REG_EXTENDED | REG_ICASE) == 0) {
     regex_compiled = 1;
     #ifdef CONSOLE_DEBUG
     printf("\033[37m%s \033[38;2;144;238;144m ►Process freeze regexes compiled.\033[39m\n", debugtag());
     printf("\033[48;2;72;19;0m\033[37m%s\n", debugtag());
     #endif
 } else {
     #ifdef CONSOLE_DEBUG
     printf("\033[37m%s \033[38;2;255;105;97m ►Failed to compile process freeze regexes.\033[39m\n", debugtag());
     #endif
 }

 int server_sock, client_sock;
    struct sockaddr_un addr;
    char buffer[256];
    mkdir("/run/yabatmand", 0755);
    unlink(SOCK_PATH);
    server_sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_sock < 0) {
        perror("socket");
        exit(1);
    }
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCK_PATH, sizeof(addr.sun_path) - 1);
    if (bind(server_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(server_sock);
        exit(1);
    }
    chmod(SOCK_PATH, 0666);
    if (listen(server_sock, 5) < 0) {
        perror("listen");
        close(server_sock);
        exit(1);
    }
merge_whitelists();
        #ifdef CONSOLE_DEBUG
        printf("\033[37m%s \033[38;2;255;255;0m Daemon ready. Listening on %s \033[39m\n",debugtag(), SOCK_PATH);
        printf("\033[48;2;72;19;0m\033[37m%s\n",debugtag());
        #endif 
    while (keep_running) {
        client_sock = accept(server_sock, NULL, NULL);
        if (client_sock < 0) {
            if (errno == EINTR) continue;
            perror("accept");
            continue;
        }
        ssize_t len = read(client_sock, buffer, sizeof(buffer) - 1);
        if (len > 0) {
            buffer[len] = '\0';
            buffer[strcspn(buffer, "\r\n")] = '\0';
            handle_command(buffer, client_sock);
        }
        close(client_sock);
    }

    /* Graceful shutdown: restore system to a safe state */
    #ifdef CONSOLE_DEBUG
    printf("\n\033[38;2;144;238;144m        ▬▬▬▬▬▬▬▬▬▬▬▬▬▬[ Shutting down, restoring defaults... ]▬▬▬▬▬▬▬▬▬▬▬▬▬▬\033[39m\n");
    #endif
    if (!reboot_or_poweroff) {
        set_normal_profile();
        unfreeze_all_frozen_services();
        freeze_processes(0);
        manage_interface(1);
        em_set_webcam_power(1);
    }
    #ifdef CONSOLE_DEBUG
    printf("\n\033[38;2;144;238;144m        ▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬[  Terminated.  ]▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬\033[39m\n");
    #endif

    close(server_sock);
    unlink(SOCK_PATH);
    if (regex_compiled) {
        regfree(&rx_msg_cached);
        regfree(&rx_sync_cached);
    }
    if (webcam_path) free(webcam_path);
    return 0;
}


