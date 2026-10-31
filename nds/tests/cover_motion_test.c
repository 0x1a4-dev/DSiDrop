#include "cover_motion.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

static void same(CoverPose actual, CoverPose expected) {
    assert(actual.x == expected.x && actual.y == expected.y && actual.size == expected.size);
}

static void between(int value, int first, int last) {
    assert(value >= (first < last ? first : last));
    assert(value <= (first > last ? first : last));
}

static void path(CoverPose from, CoverPose to) {
    CoverMotion motion;
    coverMotionStart(&motion, from, to);
    same(coverMotionPose(&motion), from);
    CoverPose previous = from;
    int firstSpeed = 0, lastSpeed = 0, peakSpeed = 0;
    unsigned previousEase = 0;
    for (unsigned frame = 1; frame <= COVER_MOTION_FRAMES; frame++) {
        coverMotionStep(&motion);
        CoverPose pose = coverMotionPose(&motion);
        assert(coverMotionEase(frame) >= previousEase);
        previousEase = coverMotionEase(frame);
        between(pose.x, previous.x, to.x);
        between(pose.y, previous.y, to.y);
        between(pose.size, previous.size, to.size);
        int speed = abs(pose.x - previous.x);
        if (frame == 1) firstSpeed = speed;
        if (frame == COVER_MOTION_FRAMES) lastSpeed = speed;
        if (speed > peakSpeed) peakSpeed = speed;
        previous = pose;
    }
    same(previous, to);
    /* Starts and lands below one pixel per frame; no old 21px first-frame jump. */
    assert(firstSpeed < COVER_MOTION_ONE && lastSpeed < COVER_MOTION_ONE);
    assert(peakSpeed <= 11 * COVER_MOTION_ONE);
    for (unsigned frame = 0; frame < 100; frame++) coverMotionStep(&motion);
    same(coverMotionPose(&motion), to);
}

static void interrupted(void) {
    CoverMotion motion;
    coverMotionStart(&motion, coverMotionSide(1), coverMotionCenter());
    /* Rapid advance/reverse, including a reversal before the first frame. */
    for (unsigned turn = 0; turn < 80; turn++) {
        for (unsigned frame = 0; frame < turn % 11; frame++) coverMotionStep(&motion);
        CoverPose visible = coverMotionPose(&motion);
        CoverPose target = turn % 3 == 0 ? coverMotionCenter() : coverMotionSide(turn & 1 ? -1 : 1);
        coverMotionStart(&motion, visible, target);
        same(coverMotionPose(&motion), visible);
        coverMotionStep(&motion);
        CoverPose next = coverMotionPose(&motion);
        between(next.x, visible.x, target.x);
        between(next.y, visible.y, target.y);
        between(next.size, visible.size, target.size);
    }
    for (unsigned frame = 0; frame < COVER_MOTION_FRAMES; frame++) coverMotionStep(&motion);
    same(coverMotionPose(&motion), motion.to);
}

int main(void) {
    assert(coverMotionEase(0) == 0);
    assert(coverMotionEase(COVER_MOTION_FRAMES) == 4096);
    assert(coverMotionEase(UINT_MAX) == 4096);
    for (int direction = -1; direction <= 1; direction += 2) {
        CoverPose center = coverMotionCenter(), side = coverMotionSide(direction);
        path(center, side); path(side, center);
        CoverMotion incoming, outgoing;
        coverMotionStart(&incoming, side, center);
        coverMotionStart(&outgoing, center, side);
        for (unsigned frame = 0; frame <= COVER_MOTION_FRAMES; frame++) {
            CoverPose in = coverMotionPose(&incoming), out = coverMotionPose(&outgoing);
            /* One shared curve keeps the two covers synchronized in size and position. */
            assert(abs(in.x + out.x - center.x - side.x) <= 1);
            assert(abs(in.y + out.y - center.y - side.y) <= 1);
            assert(abs(in.size + out.size - center.size - side.size) <= 1);
            coverMotionStep(&incoming); coverMotionStep(&outgoing);
        }
    }
    interrupted();
    puts("Cover motion: smooth endpoints, bounded steps, synchronized poses, rapid retarget/reversal and exact settling passed");
    return 0;
}
