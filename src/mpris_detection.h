#ifndef MPRIS_DETECTION_H
#define MPRIS_DETECTION_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int playing; /* 0 = stopped, 1 = playing */
    int type;    /* 0 = video, 1 = audio */
} MprisStatus;

/* Returns 0 on success, -1 if session bus is unavailable. */
int mpris_poll(MprisStatus *status);

#ifdef __cplusplus
}
#endif

#endif /* MPRIS_DETECTION_H */
