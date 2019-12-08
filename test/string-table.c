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

/** Tests the given table.

  @param table Table which should be tested.
*/
static void testStringTable(StringTable *table)
{
  assert_true(strTableGet(table, strWrap("")) == NULL);

  /* Map the lorem-ipsum chunks to the zlib chunks. */
  for(size_t index = 0; index < zlib_count; index++)
  {
    String string = strWrap(zlib_license_chunks[index]);
    if(strTableGet(table, string) != NULL)
    {
      die("string \"%s\" already exists in string table",
          zlib_license_chunks[index]);
    }

    strTableMap(table, string, &lorem_ipsum_chunks[index]);

    if(strTableGet(table, string) != &lorem_ipsum_chunks[index])
    {
      die("failed to map \"%s\" to \"%s\"", zlib_license_chunks[index],
          lorem_ipsum_chunks[index]);
    }
  }

  /* Assert that all the mappings above succeeded. */
  for(size_t index = 0; index < zlib_count; index++)
  {
    String string = strWrap(zlib_license_chunks[index]);

    if(strTableGet(table, string) != &lorem_ipsum_chunks[index])
    {
      die("\"%s\" was not mapped to \"%s\"", zlib_license_chunks[index],
          lorem_ipsum_chunks[index]);
    }
  }

  assert_true(strTableGet(table, strWrap("lingula")) == NULL);
  assert_true(strTableGet(table, strWrap("origina")) == NULL);
  assert_true(strTableGet(table, strWrap("originall")) == NULL);
}

int main(void)
{
  testGroupStart("growing string table");
  {
    assert_true(zlib_count == lorem_count);

    CR_Region *r = CR_RegionNew();
    testStringTable(strTableNew(r));
    CR_RegionRelease(r);
  }
  testGroupEnd();

  testGroupStart("multiple string tables sharing the same region");
  {
    CR_Region *r = CR_RegionNew();
    testStringTable(strTableNew(r));
    testStringTable(strTableNew(r));
    testStringTable(strTableNew(r));
    CR_RegionRelease(r);
  }
  testGroupEnd();
}
