#ifndef __INC_PICKUP_H
#define __INC_PICKUP_H

#include <stdio.h>
#include "esp_log.h"
#include "esp_err.h"


bool phone_is_picked_up(void);
void phone_pick_up(void);
void phone_hang_up(void);

#endif /* __INC_PICKUP_H */