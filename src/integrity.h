#ifndef NANO_BACKUP_SRC_INTEGRITY_H
#define NANO_BACKUP_SRC_INTEGRITY_H

#include "CRegion/region.h"
#include "metadata.h"

typedef struct ListOfBrokenPathNodes ListOfBrokenPathNodes;
struct ListOfBrokenPathNodes
{
  PathNode *node;
  ListOfBrokenPathNodes *next;
};

extern ListOfBrokenPathNodes *
checkIntegrity(CR_Region *r, Metadata *metadata, String repo_path);

#endif
