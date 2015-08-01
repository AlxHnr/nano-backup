/*
  Copyright (c) 2015 Alexander Heinrich <alxhnr@nudelpost.de>

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

/**
  @file string-table.c Tests the string table implementation.
*/

#include "string-table.h"

#include "test.h"

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

int main(void)
{
  const size_t zlib_count = sizeof(zlib_license_chunks)/sizeof(void *);
  const size_t lorem_count = sizeof(lorem_ipsum_chunks)/sizeof(void *);

  testGroupStart("map various strings");
  assert_true(zlib_count == lorem_count);

  StringTable *table = strtableNew(0);
  assert_true(strtableGet(table, str("")) == NULL);

  /* Map the lorem-ipsum chunks to the zlib chunks. */
  for(size_t index = 0; index < zlib_count; index++)
  {
    String string = str(zlib_license_chunks[index]);
    assert_true(strtableGet(table, string) == NULL);

    strtableMap(table, string, &lorem_ipsum_chunks[index]);

    assert_true(strtableGet(table, string) == &lorem_ipsum_chunks[index]);
  }

  /* Assert that all the mappings above succeeded. */
  for(size_t index = 0; index < zlib_count; index++)
  {
    String string = str(zlib_license_chunks[index]);
    assert_true(strtableGet(table, string) == &lorem_ipsum_chunks[index]);
  }

  assert_true(strtableGet(table, str("lingula")) == NULL);
  assert_true(strtableGet(table, str("origina")) == NULL);
  assert_true(strtableGet(table, str("originall")) == NULL);

  strtableFree(table);
  testGroupEnd();
}
