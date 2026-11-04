#pragma once
#include <fastrtps/config.h>
#include <fastcdr/config.h>
#ifndef FASTDDS_DSOFTBUS_ROS214_PATCH
#define FASTDDS_DSOFTBUS_ROS214_PATCH 5
#endif
static_assert(FASTRTPS_VERSION_MAJOR == 2 && FASTRTPS_VERSION_MINOR == 14 &&
    FASTRTPS_VERSION_MICRO == FASTDDS_DSOFTBUS_ROS214_PATCH,
    "ROS214 selected release/header mismatch");
#if FASTDDS_DSOFTBUS_ROS214_PATCH == 7
static_assert(FASTCDR_VERSION_MAJOR == 2 && FASTCDR_VERSION_MINOR == 2 && FASTCDR_VERSION_MICRO == 8,
    "ROS214 maintenance Fast CDR header mismatch");
#endif
