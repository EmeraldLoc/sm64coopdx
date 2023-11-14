#ifndef FIRST_PERSON_CAM_H
#define FIRST_PERSON_CAM_H

#include <stdbool.h>
#include <PR/ultratypes.h>

#define FIRST_PERSON_DEFAULT_FOV 70

struct FirstPersonCamera {
    bool enabled;
    s16 pitch;
    s16 yaw;
    f32 crouch;
    f32 fov;
};

extern struct FirstPersonCamera gFirstPersonCamera;

bool get_first_person_enabled(void);
void set_first_person_enabled(bool enable);

bool first_person_update(void);
void first_person_reset(void);

#endif