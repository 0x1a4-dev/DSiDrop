#pragma once
#include <stdint.h>

/* Positions and visible cover size use 24.8 fixed point. Keep fractional
 * positions until the OAM write, including when a gesture reverses mid-flight. */
enum { COVER_MOTION_FRAMES = 22, COVER_MOTION_ONE = 256 };
typedef struct { int x, y, size; } CoverPose;
typedef struct { CoverPose from, to; unsigned frame; } CoverMotion;

static inline unsigned coverMotionEase(unsigned frame) {
    if (frame >= COVER_MOTION_FRAMES) return 4096;
    int64_t t = (int64_t)frame * 1024 / COVER_MOTION_FRAMES;
    int64_t t3 = t * t * t;
    return (unsigned)(t3 * (10 * 1024 * 1024 - 15 * 1024 * t + 6 * t * t) / ((int64_t)1 << 38));
}

static inline CoverPose coverMotionPose(const CoverMotion *motion) {
    int ease = (int)coverMotionEase(motion->frame);
    CoverPose pose = {
        motion->from.x + (motion->to.x - motion->from.x) * ease / 4096,
        motion->from.y + (motion->to.y - motion->from.y) * ease / 4096,
        motion->from.size + (motion->to.size - motion->from.size) * ease / 4096
    };
    return pose;
}

static inline void coverMotionStart(CoverMotion *motion, CoverPose from, CoverPose to) {
    motion->from = from; motion->to = to; motion->frame = 0;
}

static inline void coverMotionStep(CoverMotion *motion) {
    if (motion->frame < COVER_MOTION_FRAMES) motion->frame++;
}

static inline CoverPose coverMotionCenter(void) {
    return (CoverPose){72 * 256, 24 * 256, 112 * 256};
}

static inline CoverPose coverMotionSide(int direction) {
    return (CoverPose){(direction < 0 ? 4 : 188) * 256, 48 * 256, 64 * 256};
}
