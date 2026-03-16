#include "map_snapshot.h"

#include <stdint.h>
#include <string.h>

struct MapSnapshotHeader
{
    char magic[4];
    uint32_t version;
    uint32_t payloadSize;
};

static const struct MapSnapshotHeader kSnapshotHeaderTemplate = {
    {'S', 'O', 'C', 'M'},
    MAP_SNAPSHOT_FORMAT_VERSION,
    (uint32_t)sizeof(struct Map)};

size_t mapSnapshotSerializedSize(void)
{
    return sizeof(struct MapSnapshotHeader) + sizeof(struct Map);
}

size_t mapSerializeSnapshot(const struct Map *map, unsigned char *buffer, size_t bufferSize)
{
    if (map == NULL || buffer == NULL || bufferSize < mapSnapshotSerializedSize())
    {
        return 0;
    }

    memcpy(buffer, &kSnapshotHeaderTemplate, sizeof(kSnapshotHeaderTemplate));
    memcpy(buffer + sizeof(kSnapshotHeaderTemplate), map, sizeof(*map));
    return mapSnapshotSerializedSize();
}

bool mapDeserializeSnapshot(struct Map *map, const unsigned char *buffer, size_t bufferSize)
{
    struct MapSnapshotHeader header;

    if (map == NULL || buffer == NULL || bufferSize != mapSnapshotSerializedSize())
    {
        return false;
    }

    memcpy(&header, buffer, sizeof(header));
    if (memcmp(header.magic, kSnapshotHeaderTemplate.magic, sizeof(header.magic)) != 0 ||
        header.version != MAP_SNAPSHOT_FORMAT_VERSION ||
        header.payloadSize != sizeof(struct Map))
    {
        return false;
    }

    memcpy(map, buffer + sizeof(header), sizeof(*map));
    return true;
}

uint32_t mapComputeSnapshotHash(const struct Map *map)
{
    const unsigned char *bytes = NULL;
    uint32_t hash = 2166136261u;

    if (map == NULL)
    {
        return 0u;
    }

    bytes = (const unsigned char *)map;
    for (size_t i = 0; i < sizeof(*map); i++)
    {
        hash ^= (uint32_t)bytes[i];
        hash *= 16777619u;
    }

    return hash;
}
