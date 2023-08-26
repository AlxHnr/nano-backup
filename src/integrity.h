#ifndef NANO_BACKUP_SRC_INTEGRITY_H
#define NANO_BACKUP_SRC_INTEGRITY_H

#include "CRegion/region.h"
#include "metadata.h"

typedef struct ListOfBrokenPathNodes ListOfBrokenPathNodes;
struct ListOfBrokenPathNodes
{
  const PathNode *node;
  ListOfBrokenPathNodes *next;
};

/** Will be called for each processed block read from the repository. Only
  files larger than FILE_HASH_SIZE will be processed. */
typedef void IntegrityProgressCallback(uint64_t processed_block_size,
                                       uint64_t total_bytes_to_process,
                                       void *user_data);

extern ListOfBrokenPathNodes *checkIntegrity(
  CR_Region *r, const Metadata *metadata, StringView repo_path,
  IntegrityProgressCallback progress_callback, void *callback_user_data);

#endif
