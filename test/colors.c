#include "colors.h"

#include <errno.h>
#include <stdlib.h>

#include "CRegion/global-region.h"

#include "safe-wrappers.h"
#include "test.h"

int main(void)
{
  testGroupStart("colorPrintf()");
  FILE *file_1 = fopen("tmp/file-1", "wb");
  assert_true(file_1 != NULL);

  colorPrintf(file_1, TC_green_bold, "This is a test");
  colorPrintf(file_1, TC_red, " file");
  colorPrintf(file_1, TC_blue, ".");
  fclose(file_1);

  FileContent file_1_content = sGetFilesContent(CR_GetGlobalRegion(), strWrap("tmp/file-1"));
  assert_true(file_1_content.size == 20);
  assert_true(memcmp(file_1_content.content, "This is a test file.", file_1_content.size) == 0);

  FILE *file_2 = fopen("tmp/file-2", "wb");
  assert_true(file_2 != NULL);

  colorPrintf(file_2, TC_yellow, "Hello");
  colorPrintf(file_2, TC_yellow, " ");
  colorPrintf(file_2, TC_green, "world");
  colorPrintf(file_2, TC_red, ".");
  fclose(file_2);

  FileContent file_2_content = sGetFilesContent(CR_GetGlobalRegion(), strWrap("tmp/file-2"));
  assert_true(file_2_content.size == 12);
  assert_true(memcmp(file_2_content.content, "Hello world.", file_2_content.size) == 0);
  testGroupEnd();
}
