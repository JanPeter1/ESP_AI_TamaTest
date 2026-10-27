#pragma once

#include <stdint.h>
#include "lvgl.h"

extern const lv_image_dsc_t cyd_avatar_cat;
extern const lv_image_dsc_t cyd_avatar_dog;
const lv_image_dsc_t *cyd_avatar_get(uint8_t avatar_id);
