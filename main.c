// Copyright (c) 2026 gadefox <gadefoxren@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include <fcntl.h>
#include <glob.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>

#define LOGS  0

#define HIBYTE(x)  (((x) >> 8) & 0xFF)
#define LOBYTE(x)  ((x) & 0xFF)

// I2C
#define I2C_WRITE(addr)  ((addr) << 1)
#define I2C_READ(addr)   (((addr) << 1) | 1)

#define I2C_MONITOR_ADDR   0x37
#define I2C_MONITOR_WRITE  I2C_WRITE(I2C_MONITOR_ADDR)

#define I2C_HOST_ADDR   0x28
#define I2C_HOST_WRITE  I2C_WRITE(I2C_HOST_ADDR)
#define I2C_HOST_READ   I2C_READ(I2C_HOST_ADDR)

// DDC
#define DDC_SIZE(x)  (0x80 | (x))

// DDC opcodes
#define GET_VCP_REQ     1
#define GET_VCP_RESP    2
#define SET_VCP         3
#define GET_TIMING_RESP 7
#define SET_TIMING      9
#define NULL_RESP       0xE1

// DDC result codes
#define DDC_SUCCESS     0
#define DDC_UNSUPPORTED 1

// VCP type
#define VCP_TYPE_PARAM      0
#define VCP_TYPE_MOMENTARY  1

// VCPs
#define VCP_BRT  0x10
#define VCP_CT   0x12

#if LOGS

void print_raw(uint8_t *data, size_t count) {
  printf("RAW (%lu bytes):", count);
  for (size_t i = 0; i < count; i++)
    printf(" %02X", data[i]);
  putchar('\n');
}

#endif  /* LOGS */

uint8_t checksum(uint8_t init, uint8_t *data, size_t count) {
  uint8_t crc = init;
  for (size_t i = 0; i < count; i++)
    crc ^= data[i];
  return crc;
}

int ddc_detect(void) {
  glob_t g;
  if (glob("/dev/i2c-*", 0, NULL, &g) != 0)
    return -1;

  int found = -1;

  for (size_t i = 0; i < g.gl_pathc; i++) {
    int fd = open(g.gl_pathv[i], O_RDWR);
    if (fd < 0)
      continue;

    if (ioctl(fd, I2C_SLAVE, I2C_MONITOR_ADDR) == 0) {
      found = fd;
      break;
    }

    close(fd);
  }

  globfree(&g);
  return found;
}

int get_vcp_resp(int fd, uint8_t vcp, uint8_t type) {
  uint8_t resp[11];

  int n = read(fd, resp, sizeof(resp));
  if (n != sizeof(resp)) {
    perror("get_vcp: read");
    return -1;
  }

#if LOGS
  print_raw(resp, n);
#endif

  if (resp[0] != I2C_MONITOR_WRITE ||
      resp[1] != DDC_SIZE(8) ||
      resp[2] != GET_VCP_RESP ||
      resp[3] != DDC_SUCCESS ||
      resp[4] != vcp ||
      resp[5] != type) {
    fprintf(stderr, "get_vcp: response");
    return -1;
  }

  uint8_t crc = checksum(I2C_HOST_WRITE, resp, sizeof(resp) - 1);
  if (crc != resp[10]) {
    fprintf(stderr, "get_vcp: checksum");
    return -1;
  }

  return (resp[8] << 8) | resp[9];
}

int set_vcp(int fd, uint8_t vcp, uint16_t val) {
  uint8_t req[7];

  req[0] = I2C_HOST_READ;
  req[1] = DDC_SIZE(4);
  req[2] = SET_VCP;
  req[3] = vcp;
  req[4] = HIBYTE(val);
  req[5] = LOBYTE(val);
  req[6] = checksum(I2C_MONITOR_WRITE, req, sizeof(req) - 1);

  if (write(fd, req, sizeof(req)) != sizeof(req)) {
    perror("set_vcp: write");
    return -1;
  }

  return 0;
}

int get_vcp(int fd, uint8_t vcp, uint8_t type) {
  uint8_t req[5];

  req[0] = I2C_HOST_READ;
  req[1] = DDC_SIZE(2);
  req[2] = GET_VCP_REQ;
  req[3] = vcp;
  req[4] = checksum(I2C_MONITOR_WRITE, req, sizeof(req) - 1);

  if (write(fd, req, sizeof(req)) != sizeof(req)) {
    perror("get_vcp: write");
    return -1;
  }

  usleep(50000);
  return get_vcp_resp(fd, vcp, type);
}

int handle_get(int fd) {
  int brt = get_vcp(fd, VCP_BRT, VCP_TYPE_PARAM);
  if (brt < 0)
    return 1;

  int ct = get_vcp(fd, VCP_CT, VCP_TYPE_PARAM);
  if (ct < 0)
    return 1;

  printf("%d %d", brt, ct);
  return 0;
}

int handle_set(int fd, char *sbrt, char *sct) {
  uint16_t brt = (uint16_t)strtol(sbrt, NULL, 10);
  if (set_vcp(fd, VCP_BRT, brt) < 0)
    return 1;

  usleep(100000);

  uint16_t ct = (uint16_t)strtol(sct, NULL, 10);
  if (set_vcp(fd, VCP_CT, ct) < 0)
    return 1;

  return 0;
}

int main(int argc, char *argv[]) {
  int fd = ddc_detect();
  if (fd < 0)
    return 1;

  int ret;
  if (argc == 3)
    ret = handle_set(fd, argv[1], argv[2]);
  else
    ret = handle_get(fd);

  close(fd);
  return ret;
}
