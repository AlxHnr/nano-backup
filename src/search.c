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
  @file search.c Implements filesystem searching via search trees.
*/

#include "search.h"

#include <stdlib.h>
#include <string.h>

#include "safe-wrappers.h"

/** A simple string buffer. */
typedef struct
{
  /** The buffer containing the string. */
  char *str;

  /** The amount of bytes used in the string. */
  size_t length;

  /** The total capacity of the allocated buffer. */
  size_t capacity;
}StringBuffer;

/** Represents a directory search state. */
typedef struct
{
  /** The stream of the current directory. */
  DIR *dir;

  /** The subnodes of the current directories node. Can be NULL. */
  SearchNode *subnodes;

  /** If a file doesn't belong to any search node and should not be
    ignored, it will be treated like a node with the policy stored in this
    variable. */
  BackupPolicy fallback_policy;
}DirSearch;

/** Represents a generic search state. */
typedef struct
{
  /** If this value is true, this state represents a DirSearch. Otherwise
    it represents a search node, that can be accessed directly without
    traversing any directory. */
  bool is_dir_search;

  /** The string length of the path of the directory to which this search
    state belongs to. Useful for restoring the buffer length, when changing
    from a directory into its parent directory. */
  size_t path_length;

  /** Unifies different ways to access files in the current directory. */
  union
  {
    /** The current node that can be accessed directly without traversing
      directories. Can be NULL, e.g. if a subnode list has reached its end.
      */
    SearchNode *current_node;

    /** The state variables of the current directories search state. */
    DirSearch search;
  }access;
}DirSearchState;

/** A stack of search states, used for storing and restoring states when
  recursing into directories. */
typedef struct
{
  /** The state array. */
  DirSearchState *state_array;

  /** The amount of used elements in the state array. */
  size_t used;

  /** The total count of allocated states. */
  size_t capacity;
}DirSearchStateStack;

struct SearchContext
{
  /** A buffer for constructing paths. */
  StringBuffer buffer;

  /* The ignore matcher list of the tree, to which the current context
     belongs to. Can be NULL. */
  StringMatcherList *ignore_matcher_list;

  /** The current search state. */
  DirSearchState state;

  /** The search states of all parent directories during recursion. */
  DirSearchStateStack state_stack;
};

/** Backups the contexts search state into its state stack. The stack will
  be reallocated if required. It will not modify the current search state
  of the given context.

  @param context A valid context with a stack capacity greater than 0.
*/
static void pushCurrentState(SearchContext *context)
{
  /* Grow state array if needed. */
  if(context->state_stack.used == context->state_stack.capacity)
  {
    context->state_stack.capacity =
      sSizeMul(context->state_stack.capacity, 2);
    context->state_stack.state_array =
      sRealloc(context->state_stack.state_array,
               sSizeMul(sizeof *context->state_stack.state_array,
                        context->state_stack.capacity));
  }

  context->state_stack.state_array[context->state_stack.used] =
    context->state;
  context->state_stack.used++;
}

/** Appends a slash and the given filename to the buffer of the given
  context. The buffer will be resized if required.

  @param context The context containing a valid buffer.
  @param filename The filename that should be appended.
*/
static void appendFilenameToBuffer(SearchContext *context, String filename)
{
  /* Add 2 extra bytes for the slash and '\0'. */
  size_t required_capacity =
    sSizeAdd(2, sSizeAdd(context->buffer.length, filename.length));
  size_t new_length = required_capacity - 1;

  /* Ensure that the new path fits into the buffer. */
  if(required_capacity > context->buffer.capacity)
  {
    context->buffer.str = sRealloc(context->buffer.str, required_capacity);
    context->buffer.capacity = required_capacity;
  }

  /* Construct the path to the file described by the current node. */
  memcpy(&context->buffer.str[context->buffer.length + 1],
         filename.str, filename.length);
  context->buffer.str[context->buffer.length] = '/';
  context->buffer.str[new_length] = '\0';
  context->buffer.length = new_length;
}

/** Constructs a SearchResult with informations from its arguments.

  @param context A valid SearchContext with a filepath in its buffer.
  @param node The node associated with the path in the contexts buffer. Can
  be NULL.
  @param policy The policy of the file in the contexts buffer.

  @return A SearchResult.
*/
static SearchResult buildSearchResult(SearchContext *context,
                                      SearchNode *node,
                                      BackupPolicy policy)
{
  struct stat stats =
    node != NULL && node->subnodes != NULL?
    sStat(context->buffer.str):
    sLStat(context->buffer.str);

  return (SearchResult)
  {
    .type =
      S_ISREG(stats.st_mode)? SRT_regular:
      S_ISLNK(stats.st_mode)? SRT_symlink:
      S_ISDIR(stats.st_mode)? SRT_directory:
      SRT_other,

    .path =
      (String)
      {
        .str = context->buffer.str,
        .length = context->buffer.length
      },

    .policy = policy,
    .stats = stats
  };
}

/** Starts a recursion step and stores it in the contexts current state. It
  will not backup the search state and will simply overwrite it.

  @param context A valid context with a valid directory path in its buffer.
  This is the directory for which the recursion will be initialised.
  @param node The node associated with the directory. Can be NULL.
  @param policy The directories policy.
*/
static void recursionStepRaw(SearchContext *context, SearchNode *node,
                             BackupPolicy policy)
{
  context->state.path_length = context->buffer.length;

  if(node != NULL &&
     node->policy == BPOL_none &&
     node->subnodes_contain_regex == false)
  {
    context->state.is_dir_search = false;
    context->state.access.current_node = node->subnodes;
  }
  else
  {
    context->state.is_dir_search = true;
    context->state.access.search.dir = sOpenDir(context->buffer.str);
    context->state.access.search.subnodes = node? node->subnodes : NULL;
    context->state.access.search.fallback_policy = policy;
  }
}

/** A wrapper around recursionStepRaw() that backups the contexts search
  state.
*/
static void recursionStep(SearchContext *context, SearchNode *node,
                          BackupPolicy policy)
{
  pushCurrentState(context);
  recursionStepRaw(context, node, policy);
}

/** Completes a search step and returns a SearchResult with informations
  from its argument. This function will not prepare the search step. It
  will only make sure, that if this search step hits a directory, a
  recursion step will be initialized.

  @param context A valid search context, with a filepath in its buffer.
  This is the filepath, for which the search step will be completed.
  @param node The node corresponding to the file in the context buffers
  filepath. Can be NULL.
  @param policy The policy for the context buffers filepath.

  @return A SearchResult.
*/
static SearchResult finishNodeStep(SearchContext *context,
                                   SearchNode *node, BackupPolicy policy)
{
  SearchResult found_file = buildSearchResult(context, node, policy);

  if(found_file.type == SRT_directory)
  {
    recursionStep(context, node, policy);
  }

  return found_file;
}

/** Pops a search state from the given contexts stack and sets it as its
  current search state. If the stack is empty, it will destroy the given
  context and end the search.

  @param context A valid SearchContext which may be destroyed by this
  function.

  @return A SearchResult with either the type SRT_end_of_directory or
  SRT_end_of_search.
*/
static SearchResult finishDirectory(SearchContext *context)
{
  if(context->state_stack.used > 0)
  {
    context->state_stack.used--;
    context->state =
      context->state_stack.state_array[context->state_stack.used];

    return (SearchResult){ .type = SRT_end_of_directory };
  }
  else
  {
    free(context->state_stack.state_array);
    free(context->buffer.str);
    free(context);

    return (SearchResult){ .type = SRT_end_of_search };
  }
}

/** Completes a search step by querying the next file from the currently
  active directory stream. If this file is a directory, a recursion step
  into it will be initialized.

  @param context A valid context which current state represents a directory
  search. It will be destroyed if the search reaches its end trough this
  search step. In this case the returned SearchResult will have the type
  SRT_end_of_search.

  @return A SearchResult.
*/
static SearchResult finishSearchStep(SearchContext *context)
{
  /* Cut path in buffer down to the search states path length. */
  context->buffer.length = context->state.path_length;
  context->buffer.str[context->buffer.length] = '\0';

  struct dirent *dir_entry =
    sReadDir(context->state.access.search.dir, context->buffer.str);

  if(dir_entry == NULL)
  {
    sCloseDir(context->state.access.search.dir, context->buffer.str);
    return finishDirectory(context);
  }

  /* Create new path for matching. */
  appendFilenameToBuffer(context, str(dir_entry->d_name));

  /* Match subnodes against dir_entry. */
  for(SearchNode *node = context->state.access.search.subnodes;
      node != NULL; node = node->next)
  {
    if(strmatch(node->matcher, dir_entry->d_name))
    {
      return finishNodeStep(context, node, node->policy);
    }
  }

  /* Match against ignore matcher. */
  for(StringMatcherList *element = context->ignore_matcher_list;
      element != NULL; element = element->next)
  {
    if(strmatch(element->matcher, context->buffer.str))
    {
      return finishSearchStep(context);
    }
  }

  /* Skip current path, if no fallback policy was defined. */
  if(context->state.access.search.fallback_policy == BPOL_none)
  {
    return finishSearchStep(context);
  }

  return finishNodeStep(context, NULL,
                        context->state.access.search.fallback_policy);
}

/** Completes a search step by directly accessing next node available in
  the search context. Counterpart to finishSearchStep().

  @param context A valid context which current state represents direct
  access. It will be destroyed, if this search step is the last step. In
  this case the returned SearchResult will have the type SRT_end_of_search.

  @return A SearchResult.
*/
static SearchResult finishCurrentNode(SearchContext *context)
{
  if(context->state.access.current_node == NULL)
  {
    return finishDirectory(context);
  }

  /* Save node and move current_node one by step. */
  SearchNode *node = context->state.access.current_node;
  context->state.access.current_node = node->next;

  String filename = strmatchGetExpression(node->matcher);
  appendFilenameToBuffer(context, filename);

  return finishNodeStep(context, node, node->policy);
}

/** Creates a new SearchContext.

  @param root The path to the directory that should be the root of the
  search.
  @param node A search tree used to search the given root path. The
  returned SearchContext will keep references into this search tree, so
  make sure not to modify it unless the search has reached its end.

  @return A new search context from which files and directories can be
  queried by using searchGetNext().
*/
SearchContext *searchNew(String root, SearchNode *node)
{
  SearchContext *context = sMalloc(sizeof *context);

  /* Initialize string buffer. */
  context->buffer.capacity =
    root.length < 128 ? 128 : sSizeAdd(root.length, 1);

  context->buffer.str = sMalloc(context->buffer.capacity);
  memcpy(context->buffer.str, root.str, root.length);
  context->buffer.str[root.length] = '\0';

  context->buffer.length = root.length;

  /* Store reference to ignore matcher list. */
  context->ignore_matcher_list = *node->ignore_matcher_list;

  /* Initialize the current search state. */
  recursionStepRaw(context, node, node->policy);

  /* Initialise the state stack. */
  context->state_stack.used = 0;
  context->state_stack.capacity = 16;
  context->state_stack.state_array =
    sMalloc(sSizeMul(sizeof *context->state_stack.state_array,
                     context->state_stack.capacity));

  return context;
}

/** Queries the next file from the given search context.

  @param context A valid search context. If the search has reached its end,
  the context will be destroyed and the returned SearchResult will have the
  type SRT_end_of_search.

  @return The next search result.
*/
SearchResult searchGetNext(SearchContext *context)
{
  if(context->state.is_dir_search)
  {
    return finishSearchStep(context);
  }
  else
  {
    return finishCurrentNode(context);
  }
}