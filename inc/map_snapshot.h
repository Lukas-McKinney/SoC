#ifndef MAP_SNAPSHOT_H
#define MAP_SNAPSHOT_H

#include "map.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MAP_SNAPSHOT_FORMAT_VERSION 1u

size_t mapSnapshotSerializedSize(void);
size_t mapSerializeSnapshot(const struct Map *map, unsigned char *buffer, size_t bufferSize);
bool mapDeserializeSnapshot(struct Map *map, const unsigned char *buffer, size_t bufferSize);
uint32_t mapComputeSnapshotHash(const struct Map *map);

#endif
