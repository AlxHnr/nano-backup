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
  @file string-matcher.c Tests functions for simple string matching.
*/

#include "string-matcher.h"

#include "test.h"
#include "string-utils.h"

int main(void)
{
  String zero_length = (String){ .str = "some-data", .length = 0 };

  testGroupStart("strmatchString()");
  StringMatcher *empty_str       = strmatchString(str(""),       10);
  StringMatcher *foo_str         = strmatchString(str("foo"),    25);
  StringMatcher *foo_bar_str     = strmatchString(str("foobar"), 40);
  StringMatcher *zero_length_str = strmatchString(zero_length,   54);

  assert_true(empty_str       != NULL);
  assert_true(foo_str         != NULL);
  assert_true(foo_bar_str     != NULL);
  assert_true(zero_length_str != NULL);
  testGroupEnd();

  testGroupStart("strmatchRegex()");
  /* The suffixes '_b', '_e' and '_f' indicate whether a regex matcher was
     build with beginning (^), end ($) or full (^$) matching operators. */
  StringMatcher *empty_rx   = strmatchRegex(str(""),   55);
  StringMatcher *empty_rx_b = strmatchRegex(str("^"),  70);
  StringMatcher *empty_rx_e = strmatchRegex(str("$"),  70);
  StringMatcher *empty_rx_f = strmatchRegex(str("^$"), 85);

  StringMatcher *foo_rx   = strmatchRegex(str("foo"),   100);
  StringMatcher *foo_rx_b = strmatchRegex(str("^foo"),  115);
  StringMatcher *foo_rx_e = strmatchRegex(str("foo$"),  130);
  StringMatcher *foo_rx_f = strmatchRegex(str("^foo$"), 145);

  StringMatcher *foobar_rx   = strmatchRegex(str("foobar"),   160);
  StringMatcher *foobar_rx_b = strmatchRegex(str("^foobar"),  190);
  StringMatcher *foobar_rx_e = strmatchRegex(str("foobar$"),  190);
  StringMatcher *foobar_rx_f = strmatchRegex(str("^foobar$"), 205);

  StringMatcher *all_re         = strmatchRegex(str(".*"),   327);
  StringMatcher *all_re_f       = strmatchRegex(str("^.*$"), 873);
  StringMatcher *zero_length_re = strmatchRegex(zero_length, 1254);

  assert_true(empty_rx       != NULL);
  assert_true(empty_rx_b     != NULL);
  assert_true(empty_rx_e     != NULL);
  assert_true(empty_rx_f     != NULL);
  assert_true(foo_rx         != NULL);
  assert_true(foo_rx_b       != NULL);
  assert_true(foo_rx_e       != NULL);
  assert_true(foo_rx_f       != NULL);
  assert_true(foobar_rx      != NULL);
  assert_true(foobar_rx_b    != NULL);
  assert_true(foobar_rx_e    != NULL);
  assert_true(foobar_rx_f    != NULL);
  assert_true(all_re         != NULL);
  assert_true(all_re_f       != NULL);
  assert_true(zero_length_re != NULL);

  assert_error(strmatchRegex(str("?"), 220), "config: line 220: "
               "Invalid preceding regular expression: \"?\"");
  assert_error(strmatchRegex(str("(foo|bar"), 235), "config: line 235: "
               "Unmatched ( or \\(: \"(foo|bar\"");
  testGroupEnd();

  testGroupStart("strmatch()");
  assert_true(strmatch(empty_str, ""));
  assert_true(strmatch(empty_str, "foo") == false);

  assert_true(strmatch(foo_str,  "foo"));
  assert_true(strmatch(foo_str, "foobar") == false);

  assert_true(strmatch(foo_bar_str, "foobar"));
  assert_true(strmatch(foo_bar_str, "foo") == false);

  assert_true(strmatch(zero_length_str, ""));
  assert_true(strmatch(zero_length_str, "some")          == false);
  assert_true(strmatch(zero_length_str, "some-data")     == false);
  assert_true(strmatch(zero_length_str, zero_length.str) == false);

  assert_true(strmatch(empty_rx,   ""));
  assert_true(strmatch(empty_rx_b, ""));
  assert_true(strmatch(empty_rx_e, ""));
  assert_true(strmatch(empty_rx_f, ""));
  assert_true(strmatch(all_re,     ""));
  assert_true(strmatch(all_re_f,   ""));

  assert_true(strmatch(foo_rx,   "foo"));
  assert_true(strmatch(foo_rx_b, "foo"));
  assert_true(strmatch(foo_rx_e, "foo"));
  assert_true(strmatch(foo_rx_f, "foo"));
  assert_true(strmatch(all_re,   "foo"));
  assert_true(strmatch(all_re_f, "foo"));
  assert_true(strmatch(foo_rx,   "foobar") == false);

  assert_true(strmatch(foobar_rx,   "foobar"));
  assert_true(strmatch(foobar_rx_b, "foobar"));
  assert_true(strmatch(foobar_rx_e, "foobar"));
  assert_true(strmatch(foobar_rx_f, "foobar"));
  assert_true(strmatch(all_re,      "foobar"));
  assert_true(strmatch(all_re_f,    "foobar"));
  assert_true(strmatch(foobar_rx,   "foo") == false);

  assert_true(strmatch(zero_length_re, ""));
  assert_true(strmatch(zero_length_re, "some")          == false);
  assert_true(strmatch(zero_length_re, "some-data")     == false);
  assert_true(strmatch(zero_length_re, zero_length.str) == false);
  testGroupEnd();

  testGroupStart("strmatchHasMatched()");
  assert_true(strmatchHasMatched(empty_str));
  assert_true(strmatchHasMatched(foo_str));
  assert_true(strmatchHasMatched(foo_bar_str));
  assert_true(strmatchHasMatched(zero_length_str));

  assert_true(strmatchHasMatched(empty_rx));
  assert_true(strmatchHasMatched(empty_rx_b));
  assert_true(strmatchHasMatched(empty_rx_e));
  assert_true(strmatchHasMatched(empty_rx_f));

  assert_true(strmatchHasMatched(foo_rx));
  assert_true(strmatchHasMatched(foo_rx_b));
  assert_true(strmatchHasMatched(foo_rx_e));
  assert_true(strmatchHasMatched(foo_rx_f));

  assert_true(strmatchHasMatched(foobar_rx));
  assert_true(strmatchHasMatched(foobar_rx_b));
  assert_true(strmatchHasMatched(foobar_rx_e));
  assert_true(strmatchHasMatched(foobar_rx_f));

  assert_true(strmatchHasMatched(all_re));
  assert_true(strmatchHasMatched(all_re_f));
  assert_true(strmatchHasMatched(zero_length_re));

  /* Create some string matcher which have never matched anything. */
  StringMatcher *nomatch_str  = strmatchString(str("nomatch"), 4567);
  StringMatcher *nomatch_re   = strmatchRegex(str("nomatch"),  7654);
  StringMatcher *nomatch_re_b = strmatchRegex(str("^nomatch"), 9612);

  assert_true(nomatch_str  != NULL);
  assert_true(nomatch_re   != NULL);
  assert_true(nomatch_re_b != NULL);

  assert_true(strmatchHasMatched(nomatch_str)  == false);
  assert_true(strmatchHasMatched(nomatch_re)   == false);
  assert_true(strmatchHasMatched(nomatch_re_b) == false);

  strmatch(nomatch_str, "");
  strmatch(nomatch_str, "match");

  strmatch(nomatch_re, "");
  strmatch(nomatch_re, "nonomatches");

  strmatch(nomatch_re_b, "");
  strmatch(nomatch_re_b, "nomatches");

  assert_true(strmatchHasMatched(nomatch_str)  == false);
  assert_true(strmatchHasMatched(nomatch_re)   == false);
  assert_true(strmatchHasMatched(nomatch_re_b) == false);

  strmatch(nomatch_str,  "nomatch");
  strmatch(nomatch_re,   "nomatch");
  strmatch(nomatch_re_b, "nomatch");

  assert_true(strmatchHasMatched(nomatch_str));
  assert_true(strmatchHasMatched(nomatch_re));
  assert_true(strmatchHasMatched(nomatch_re_b));
  testGroupEnd();

  testGroupStart("strmatchLineNr()");
  assert_true(strmatchLineNr(strmatchString(str("foo"), 0))   == 0);
  assert_true(strmatchLineNr(strmatchString(str("foo"), 321)) == 321);
  assert_true(strmatchLineNr(strmatchRegex(str("foo"),  321)) == 321);
  assert_true(strmatchLineNr(strmatchRegex(str("foo"),  12))  == 12);

  assert_true(strmatchLineNr(empty_str)       == 10);
  assert_true(strmatchLineNr(foo_str)         == 25);
  assert_true(strmatchLineNr(foo_bar_str)     == 40);
  assert_true(strmatchLineNr(zero_length_str) == 54);
  assert_true(strmatchLineNr(all_re)          == 327);
  assert_true(strmatchLineNr(all_re_f)        == 873);
  assert_true(strmatchLineNr(zero_length_re)  == 1254);
  testGroupEnd();

  testGroupStart("strmatchGetExpression()");
  assert_true(strCompare(strmatchGetExpression(empty_str), str("")));
  assert_true(strCompare(strmatchGetExpression(foo_bar_str), str("foobar")));
  assert_true(strCompare(strmatchGetExpression(zero_length_str), str("")));

  assert_true(strCompare(strmatchGetExpression(empty_rx), str("")));
  assert_true(strCompare(strmatchGetExpression(foo_rx),   str("foo")));
  assert_true(strCompare(strmatchGetExpression(foo_rx_b), str("^foo")));
  assert_true(strCompare(strmatchGetExpression(foo_rx_e), str("foo$")));
  assert_true(strCompare(strmatchGetExpression(foo_rx_f), str("^foo$")));
  assert_true(strCompare(strmatchGetExpression(all_re),   str(".*")));
  assert_true(strCompare(strmatchGetExpression(all_re_f), str("^.*$")));

  /* Assert that the expression gets captured, but not copied. */
  assert_true(strmatchGetExpression(zero_length_str).str == zero_length.str);
  assert_true(strmatchGetExpression(zero_length_re).str == zero_length.str);
  testGroupEnd();
}
