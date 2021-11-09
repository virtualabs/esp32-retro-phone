#ifndef __INC_STATUS_H

typedef enum {
  PHONE_BOOT,
  PHONE_WIFI,
  PHONE_CONN,
  PHONE_IDLE,
  PHONE_RING,
  PHONE_ERROR
} phone_status_t;

#endif /* __INC_STATUS_H */