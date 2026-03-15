#ifndef RESOURCE_H
#define RESOURCE_H

enum ResourceType {
    RESOURCE_WOOD,
    RESOURCE_WHEAT,
    RESOURCE_CLAY,
    RESOURCE_SHEEP,
    RESOURCE_STONE
};

struct Resource {
    enum ResourceType type;
};

#endif
