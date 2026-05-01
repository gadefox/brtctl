// Copyright (C) 2026 gadefox <gadefoxren@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/*
 * DDC result codes:
 *  0x00 = No error
 *  0x01 = Unsupported VCP code
 */

/*
 * DDC opcodes:
 *  0x01 = GET_VCP_REQUEST
 *  0x02 = GET_VCP_REPLY
 *  0x03 = SET_VCP
 *  0x07 = GET_TIMING_REPLY
 *  0x09 = SET_TIMING
 *  0xE1 = NULL_RESPONSE
 */

#include <fcntl.h>
#include <glob.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>

#define DEBUG  0

void print_raw(uint8_t *data, size_t count) {
  printf("RAW (%d bytes):", count);
  for (size_t i = 0; i < count; i++)
    printf(" %02X", data[i]);
  printf("\n");
}

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

    if (ioctl(fd, I2C_SLAVE, 0x37) == 0) {
      found = fd;
      break;
    }

    close(fd);
  }

  globfree(&g);
  return found;
}

int get_vcp_resp(int fd, uint8_t vcp) {
  uint8_t resp[11];
  int n = read(fd, resp, sizeof(resp));
  if (n != sizeof(resp)) {
    perror("get_vcp: read");
    return -1;
  }

#if DEBUG
  print_raw(resp, n);
#endif

  if (resp[0] != 0x6E ||  // DDC_ADDR << 1
      resp[1] != 0x88 ||  // len = data & 0x7F (8)
      resp[2] != 0x02 ||  // GET_VCP_REPLY
      resp[3] != 0x00 ||  // result code
      resp[4] != vcp) {
    fprintf(stderr, "get_vcp: response");
    return -1;
  }

  uint8_t crc = checksum(0xB4, resp + 4, 6);  // 0xB4 = 0x50 ^ 0x6E ^ 0x88 ^ 0x02 ^ 0x00
  if (crc != resp[10]) {
    fprintf(stderr, "get_vcp: checksum");
    return -1;
  }

  return (resp[8] << 8) | resp[9];
}

int set_vcp(int fd, uint8_t vcp, uint16_t val) {
  uint8_t req[7];
  req[0] = 0x51;
  req[1] = 0x84;  // length (0x80 + 4 bytes)
  req[2] = 0x03;  // SET_VCP opcode
  req[3] = vcp;
  req[4] = val >> 8;
  req[5] = val & 0xFF;
  req[6] = checksum(0xB8, req + 3, 3);  // 0xB8 = 0x6E ^ 0x51 ^ 0x84 ^ 0x03

  if (write(fd, req, sizeof(req)) != sizeof(req)) {
    perror("set_vcp: write");
    return -1;
  }

  return 0;
}

int get_vcp(int fd, uint8_t vcp) {
  uint8_t req[5];
  req[0] = 0x51;
  req[1] = 0x82;  // length (0x80 + 2 bytes)
  req[2] = 0x01;  // GET_VCP opcode
  req[3] = vcp;
  req[4] = 0xBC ^ vcp;  // 0xBC = 0x6E ^ 0x51 ^ 0x82 ^ 0x01

  if (write(fd, req, sizeof(req)) != sizeof(req)) {
    perror("get_vcp: write");
    return -1;
  }

  usleep(50000);
  return get_vcp_resp(fd, vcp);
}

int handle_get(int fd) {
  int val10h = get_vcp(fd, 0x10);
  if (val10h < 0)
    return 1;

  int val12h = get_vcp(fd, 0x12);
  if (val12h < 0)
    return 1;

  printf("%d %d", val10h, val12h);
  return 0;
}

int handle_set(int fd, char *arg10h, char *arg12h) {
  uint16_t val10h = (uint16_t)strtol(arg10h, NULL, 10);
  if (set_vcp(fd, 0x10, val10h) < 0)
    return 1;

  usleep(50000);

  uint16_t val12h = (uint16_t)strtol(arg12h, NULL, 10);
  if (set_vcp(fd, 0x12, val12h) < 0)
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
