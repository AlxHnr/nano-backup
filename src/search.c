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
  Implements filesystem searching via search trees.
*/

#include "search.h"

#include <stdlib.h>
#include <string.h>

#include "safe-wrappers.h"

/** A simple string buffer. */
typedef struct
{
  /** A null-terminated buffer, containing the string. */
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

  /* The ignore expression list of the tree, to which the current context
     belongs to. Can be NULL. */
  RegexList *ignore_expressions;

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

/** Appends the given filename to the currently traversed path. The
  contexts buffer will be resized if required.

  @param context The context with the buffer that should be updated.
  @param filename The filename that should be appended.
*/
static void setPathToFile(SearchContext *context, String filename)
{
  /* Add 2 extra bytes for the slash and '\0'. */
  size_t required_capacity =
    sSizeAdd(2, sSizeAdd(context->state.path_length, filename.length));
  size_t new_length = required_capacity - 1;

  /* Ensure that the new path fits into the buffer. */
  if(required_capacity > context->buffer.capacity)
  {
    context->buffer.str = sRealloc(context->buffer.str, required_capacity);
    context->buffer.capacity = required_capacity;
  }

  /* Construct the path to the file described by the current node. */
  memcpy(&context->buffer.str[context->state.path_length + 1],
         filename.str, filename.length);
  context->buffer.str[context->state.path_length] = '/';
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

    .node = node,
    .policy = policy,
    .stats = stats,
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
  /* Store the directories path length before recursing into it. */
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

  if(node != NULL)
  {
    node->search_match |= found_file.type;
  }

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
  struct dirent *dir_entry =
    sReadDir(context->state.access.search.dir, context->buffer.str);

  if(dir_entry == NULL)
  {
    sCloseDir(context->state.access.search.dir, context->buffer.str);
    return finishDirectory(context);
  }

  /* Create new path for matching. */
  String dir_entry_name = str(dir_entry->d_name);
  setPathToFile(context, dir_entry_name);

  /* Match subnodes against dir_entry. */
  for(SearchNode *node = context->state.access.search.subnodes;
      node != NULL; node = node->next)
  {
    if(node->regex)
    {
      if(regexec(node->regex, dir_entry_name.str, 0, NULL, 0) == 0)
      {
        return finishNodeStep(context, node, node->policy);
      }
    }
    else if(strCompare(node->name, dir_entry_name))
    {
      return finishNodeStep(context, node, node->policy);
    }
  }

  /* Skip current path, if no fallback policy was defined. */
  if(context->state.access.search.fallback_policy == BPOL_none)
  {
    return finishSearchStep(context);
  }

  /* Match against ignore expressions. */
  for(RegexList *element = context->ignore_expressions;
      element != NULL; element = element->next)
  {
    if(regexec(element->regex, context->buffer.str, 0, NULL, 0) == 0)
    {
      element->has_matched = true;
      return finishSearchStep(context);
    }
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

  setPathToFile(context, node->name);

  if(sPathExists(context->buffer.str))
  {
    return finishNodeStep(context, node, node->policy);
  }
  else
  {
    return finishCurrentNode(context);
  }
}

/** Creates a new SearchContext for searching the filesystem.

  @param root_node A search tree used for searching the filesystem. This
  tree will be modified during search to denote nodes that have matched an
  existing file. See the documentation of SearchNode for more informations.
  The returned SearchContext will keep references into this tree, so make
  sure not to modify it as long as the context is in use.

  @return A new search context from which files and directories can be
  queried by using searchGetNext(). It will be automatically destroyed if
  the search has reached its end.
*/
SearchContext *searchNew(SearchNode *root_node)
{
  SearchContext *context = sMalloc(sizeof *context);

  /* Initialize string buffer. */
  context->buffer.capacity = 8;
  context->buffer.str = sMalloc(context->buffer.capacity);

  /* Initialize a search step into "/". */
  context->buffer.str[0] = '/';
  context->buffer.str[1] = '\0';
  context->buffer.length = 1;

  recursionStepRaw(context, root_node, root_node->policy);

  /* Prevent found paths from starting with two slashes. */
  context->state.path_length = 0;

  /* Store reference to ignore expression list. */
  context->ignore_expressions = *root_node->ignore_expressions;

  /* Initialise the state stack. */
  context->state_stack.used = 0;
  context->state_stack.capacity = 4;
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
