#pragma once

#define LV_SCREEN_LOAD_ANIM_MOVE_LEFT LV_SCR_LOAD_ANIM_MOVE_LEFT
#define LV_SCREEN_LOAD_ANIM_MOVE_RIGHT LV_SCR_LOAD_ANIM_MOVE_RIGHT
#define LV_SCREEN_LOAD_ANIM_MOVE_TOP LV_SCR_LOAD_ANIM_MOVE_TOP
#define LV_SCREEN_LOAD_ANIM_MOVE_BOTTOM LV_SCR_LOAD_ANIM_MOVE_BOTTOM

#define lv_indev_set_gesture_min_distance(indev, distance) ((void)(indev), (void)(distance))
#define lv_indev_set_gesture_min_velocity(indev, velocity) ((void)(indev), (void)(velocity))
