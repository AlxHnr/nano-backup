/*
  Copyright (c) 2016 Alexander Heinrich

  This software is provided 'as-is', without any express or implied
  warranty. In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

     1. The origin of this software must not be misrepresented; you must
        not claim that you wrote the original software. If you use this
        software in a product, an acknowledgment in the product
        documentation would be appreciated but is not required.

     2. Altered source versions must be plainly marked as such, and must
        not be misrepresented as being the original software.

     3. This notice may not be removed or altered from any source
        distribution.
*/

/** @file
  Tests the string table implementation.
*/

#include "string-table.h"

#include "test.h"
#include "error-handling.h"

static const char *zlib_license_chunks[] =
{
  "original", "purpose,", "documentation", "use", "arising", "as", "",
  "'as-is',", "that", "software", "redistribute", "or", "is", "provided",
  "required.", "removed", "wrote", "source", "in", "plainly", "any", "you",
  "such,", "anyone", "distribution.", "of", "software.", "claim", "for",
  "applications,", "liable", "and", "notice", "altered", "subject",
  "Altered", "a", "If", "will", "held", "no", "granted", "This", "authors",
  "may", "commercial", "alter", "from", "In", "Permission",
  "misrepresented;", "an", "product,", "not", "2.", "product", "being",
  "it", "The", "express", "event", "appreciated", "the", "versions", "1.",
  "implied", "to", "be", "marked", "must", "this", "misrepresented",
  "warranty.", "acknowledgment", "following", "restrictions:", "origin",
  "damages", "freely,", "3.", "including", "but", "would", "without",
};
static const size_t zlib_count = sizeof(zlib_license_chunks)/sizeof(void*);

static const char *lorem_ipsum_chunks[] =
{
  "ligula", "mattis", "feugiat", "id", "amet", "consequat", "mollis",
  "magnis", "odio", "Ut", "Donec", "lorem", "gravida", "lectus.", "enim,",
  "et", "felis,", "nisl", "Praesent", "a", "at", "Maecenas", "dapibus",
  "parturient", "lacinia", "magna", "quam", "imperdiet.", "Aenean", "dis",
  "ante", "sed,", "nisi", "consectetur", "Lorem", "elit.", "hendrerit.",
  "amet,", "pulvinar", "Pellentesque", "consectetur.", "sociis", "elit",
  "sed", "in", "non", "dolor", "montes,", "quis", "adipiscing", "natoque",
  "eget", "lorem.", "congue", "mauris.", "Curabitur", "nec", "ac",
  "libero", "Sed", "augue.", "porta", "sagittis.", "ipsum", "rhoncus.",
  "egestas", "auctor", "diam", "dolor.", "accumsan.", "convallis",
  "penatibus", "arcu", "eros.", "nascetur", "foo", "sit", "pharetra",
  "Nam", "semper", "enim", "mi", "malesuada", "",
};
static const size_t lorem_count = sizeof(lorem_ipsum_chunks)/sizeof(void*);

/** Tests the given StringTable.

  @param table The table which should be tested.
  @param spam_strtable_free True, if the table should be passed to
  strtableFree() permanently. This can be used to test that strtableFree()
  ignores fixed-size string tables.
*/
static void testStringTable(StringTable *table, bool spam_strtable_free)
{
  if(spam_strtable_free) strtableFree(table);
  assert_true(strtableGet(table, str("")) == NULL);

  /* Map the lorem-ipsum chunks to the zlib chunks. */
  for(size_t index = 0; index < zlib_count; index++)
  {
    if(spam_strtable_free) strtableFree(table);
    String string = str(zlib_license_chunks[index]);
    if(strtableGet(table, string) != NULL)
    {
      die("string \"%s\" already exists in string table",
          zlib_license_chunks[index]);
    }

    strtableMap(table, string, &lorem_ipsum_chunks[index]);
    if(spam_strtable_free) strtableFree(table);

    if(strtableGet(table, string) != &lorem_ipsum_chunks[index])
    {
      die("failed to map \"%s\" to \"%s\"", zlib_license_chunks[index],
          lorem_ipsum_chunks[index]);
    }
  }

  /* Assert that all the mappings above succeeded. */
  for(size_t index = 0; index < zlib_count; index++)
  {
    String string = str(zlib_license_chunks[index]);

    if(spam_strtable_free) strtableFree(table);
    if(strtableGet(table, string) != &lorem_ipsum_chunks[index])
    {
      die("\"%s\" was not mapped to \"%s\"", zlib_license_chunks[index],
          lorem_ipsum_chunks[index]);
    }
  }

  if(spam_strtable_free) strtableFree(table);
  assert_true(strtableGet(table, str("lingula")) == NULL);
  assert_true(strtableGet(table, str("origina")) == NULL);
  assert_true(strtableGet(table, str("originall")) == NULL);
}

int main(void)
{
  testGroupStart("dynamic string table");
  assert_true(zlib_count == lorem_count);

  StringTable *table = strtableNew();
  testStringTable(table, false);
  strtableFree(table);
  testGroupEnd();

  testGroupStart("fixed table with size 0");
  assert_error(strtableNewFixed(0), "memory pool: unable to allocate 0 bytes");
  testGroupEnd();

  testGroupStart("fixed table with size 1");
  testStringTable(strtableNewFixed(1), true);
  testStringTable(strtableNewFixed(1), false);
  testGroupEnd();

  testGroupStart("fixed table with size 8");
  testStringTable(strtableNewFixed(8), true);
  testStringTable(strtableNewFixed(8), false);
  testGroupEnd();

  testGroupStart("fixed table with size 64");
  testStringTable(strtableNewFixed(64), true);
  testStringTable(strtableNewFixed(64), false);
  testGroupEnd();

  testGroupStart("fixed table with size 4096");
  testStringTable(strtableNewFixed(4096), true);
  testStringTable(strtableNewFixed(4096), false);
  testGroupEnd();
}
