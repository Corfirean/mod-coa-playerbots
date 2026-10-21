#ifndef COA_PLAYERBOTS_TEST_MOTION_MASTER_STUB_H
#define COA_PLAYERBOTS_TEST_MOTION_MASTER_STUB_H

#include "Define.h"

// Exact enum MovementGeneratorType values from AzerothCore src/server/game/Movement/MotionMaster.h
enum MovementGeneratorType : uint8
{
    IDLE_MOTION_TYPE                = 0,
    RANDOM_MOTION_TYPE              = 1,
    WAYPOINT_MOTION_TYPE            = 2,
    MAX_DB_MOTION_TYPE              = 3,
    ANIMAL_RANDOM_MOTION_TYPE       = MAX_DB_MOTION_TYPE,
    CONFUSED_MOTION_TYPE            = 4,
    CHASE_MOTION_TYPE               = 5,
    HOME_MOTION_TYPE                = 6,
    FLIGHT_MOTION_TYPE              = 7,
    POINT_MOTION_TYPE               = 8,
    FLEEING_MOTION_TYPE             = 9,
    DISTRACT_MOTION_TYPE            = 10,
    ASSISTANCE_MOTION_TYPE          = 11,
    ASSISTANCE_DISTRACT_MOTION_TYPE = 12,
    TIMED_FLEEING_MOTION_TYPE       = 13,
    FOLLOW_MOTION_TYPE              = 14,
    ROTATE_MOTION_TYPE              = 15,
    EFFECT_MOTION_TYPE              = 16,
    ESCORT_MOTION_TYPE              = 17,
};

#endif // COA_PLAYERBOTS_TEST_MOTION_MASTER_STUB_H
