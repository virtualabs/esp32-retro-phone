#include "pickup.h"

static volatile bool gb_picked_up = false;

bool phone_is_picked_up(void)
{
  return gb_picked_up;
}

void phone_pick_up(void)
{
  gb_picked_up = true;
}

void phone_hang_up(void)
{
  gb_picked_up = false;
}