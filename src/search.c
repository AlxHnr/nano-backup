#include "search.h"

#include <stdlib.h>
#include <string.h>

#include "error-handling.h"
#include "informations.h"
#include "safe-math.h"
#include "safe-wrappers.h"

typedef struct
{
  /** A null-terminated buffer, containing the string. */
  char *str;
  size_t length;
  size_t capacity;
} StringBuffer;

typedef struct
{
  DirIterator *dir;

  /** The subnodes of the current directories node. Can be NULL. */
  SearchNode *subnodes;

  /** If a file doesn't belong to any search node and should not be
    ignored, it will be treated like a node with the policy stored in this
    variable. */
  BackupPolicy fallback_policy;
} DirSearch;

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
  } access;
} DirSearchState;

/** A stack of search states, used for storing and restoring states when
  recursing into directories. */
typedef struct
{
  DirSearchState *state_array;
  size_t capacity;
  size_t used;

} DirSearchStateStack;

struct SearchIterator
{
  /** A buffer for constructing paths. */
  StringBuffer buffer;

  /* The ignore expression list of the tree, to which the current iterator
     belongs to. Can be NULL. */
  RegexList *ignore_expressions;

  DirSearchState state;

  /** The search states of all parent directories during recursion. */
  DirSearchStateStack state_stack;
};

/** Backups the iterators search state into its state stack. The stack will
  be reallocated if required. It will not modify the current search state
  of the given iterator.

  @param iterator A valid iterator with a stack capacity greater than 0.
*/
static void pushCurrentState(SearchIterator *iterator)
{
  /* Grow state array if needed. */
  if(iterator->state_stack.used == iterator->state_stack.capacity)
  {
    iterator->state_stack.capacity =
      sSizeMul(iterator->state_stack.capacity, 2);
    iterator->state_stack.state_array =
      sRealloc(iterator->state_stack.state_array,
               sSizeMul(sizeof *iterator->state_stack.state_array,
                        iterator->state_stack.capacity));
  }

  iterator->state_stack.state_array[iterator->state_stack.used] =
    iterator->state;
  iterator->state_stack.used++;
}

/** Appends the given filename to the currently traversed path. The
  iterators buffer will be resized if required.

  @param iterator The iterator with the buffer that should be updated.
  @param filename The filename that should be appended.
*/
static void setPathToFile(SearchIterator *iterator, StringView filename)
{
  /* Add 2 extra bytes for the slash and '\0'. */
  const size_t required_capacity =
    sSizeAdd(2, sSizeAdd(iterator->state.path_length, filename.length));
  const size_t new_length = required_capacity - 1;

  /* Ensure that the new path fits into the buffer. */
  if(required_capacity > iterator->buffer.capacity)
  {
    iterator->buffer.str =
      sRealloc(iterator->buffer.str, required_capacity);
    iterator->buffer.capacity = required_capacity;
  }

  /* Construct the path to the file described by the current node. */
  memcpy(&iterator->buffer.str[iterator->state.path_length + 1],
         filename.content, filename.length);
  iterator->buffer.str[iterator->state.path_length] = '/';
  iterator->buffer.str[new_length] = '\0';
  iterator->buffer.length = new_length;
}

/**
  @param iterator A valid SearchIterator with a filepath in its buffer.
  @param node The node associated with the path in the iterators buffer.
  Can be NULL.
  @param policy The policy of the file in the iterators buffer.
*/
static SearchResult buildSearchResult(const SearchIterator *iterator,
                                      const SearchNode *node,
                                      const BackupPolicy policy)
{
  const struct stat stats = node != NULL && node->subnodes != NULL
    ? sStat(str(iterator->buffer.str))
    : sLStat(str(iterator->buffer.str));

  return (SearchResult){
    .type = S_ISREG(stats.st_mode) ? SRT_regular_file
      : S_ISLNK(stats.st_mode)     ? SRT_symlink
      : S_ISDIR(stats.st_mode)     ? SRT_directory
                                   : SRT_other,

    .path = strUnterminated(iterator->buffer.str, iterator->buffer.length),

    .node = node,
    .policy = policy,
    .stats = stats,
  };
}

/** Starts a recursion step and stores it in the iterators current state.
  It will not backup the search state and will simply overwrite it.

  @param iterator A valid iterator with a valid directory path in its
  buffer. This is the directory for which the recursion will be
  initialised.
  @param node The node associated with the directory. Can be NULL.
  @param policy The directories policy.
*/
static void recursionStepRaw(SearchIterator *iterator, SearchNode *node,
                             const BackupPolicy policy)
{
  /* Store the directories path length before recursing into it. */
  iterator->state.path_length = iterator->buffer.length;

  if(node != NULL && node->policy == BPOL_none &&
     !node->subnodes_contain_regex)
  {
    iterator->state.is_dir_search = false;
    iterator->state.access.current_node = node->subnodes;
  }
  else
  {
    iterator->state.is_dir_search = true;
    iterator->state.access.search.dir =
      sDirOpen(str(iterator->buffer.str));
    iterator->state.access.search.subnodes = node ? node->subnodes : NULL;
    iterator->state.access.search.fallback_policy = policy;
  }
}

static void recursionStep(SearchIterator *iterator, SearchNode *node,
                          const BackupPolicy policy)
{
  pushCurrentState(iterator);
  recursionStepRaw(iterator, node, policy);
}

/** Completes a search step and returns a SearchResult with informations
  from its argument. This function will not prepare the search step. It
  will only make sure, that if this search step hits a directory, a
  recursion step will be initialized.

  @param iterator A valid search iterator, with a filepath in its buffer.
  This is the filepath, for which the search step will be completed.
  @param node The node corresponding to the file in the iterator buffers
  filepath. Can be NULL.
  @param policy The policy for the iterator buffers filepath.
*/
static SearchResult finishNodeStep(SearchIterator *iterator,
                                   SearchNode *node,
                                   const BackupPolicy policy)
{
  SearchResult found_file = buildSearchResult(iterator, node, policy);

  if(node != NULL)
  {
    node->search_match |= found_file.type;
  }

  if(found_file.type == SRT_directory)
  {
    recursionStep(iterator, node, policy);
  }

  return found_file;
}

/** Pops a search state from the given iterators stack and sets it as its
  current search state. If the stack is empty, it will destroy the given
  iterator and end the search.

  @param iterator A valid SearchIterator which may be destroyed by this
  function.

  @return A SearchResult with either the type SRT_end_of_directory or
  SRT_end_of_search.
*/
static SearchResult finishDirectory(SearchIterator *iterator)
{
  if(iterator->state_stack.used > 0)
  {
    iterator->state_stack.used--;
    iterator->state =
      iterator->state_stack.state_array[iterator->state_stack.used];

    return (SearchResult){ .type = SRT_end_of_directory };
  }

  free(iterator->state_stack.state_array);
  free(iterator->buffer.str);
  free(iterator);

  return (SearchResult){ .type = SRT_end_of_search };
}

static bool nodeMatches(const SearchNode *node, StringView string)
{
  if(node->regex)
  {
    return regexec(node->regex, string.content, 0, NULL, 0) == 0;
  }

  return strEqual(node->name, string);
}

/** Completes a search step by querying the next file from the currently
  active directory stream. If this file is a directory, a recursion step
  into it will be initialized.

  @param iterator A valid iterator which current state represents a
  directory search. It will be destroyed if the search reaches its end
  trough this search step. In this case the returned SearchResult will have
  the type SRT_end_of_search.
*/
static SearchResult finishSearchStep(SearchIterator *iterator)
{
  StringView entry = sDirGetNext(iterator->state.access.search.dir);
  if(entry.length == 0)
  {
    sDirClose(iterator->state.access.search.dir);
    return finishDirectory(iterator);
  }

  /* Create new path for matching. */
  StringView dir_entry_name = strSplitPath(entry).tail;
  setPathToFile(iterator, dir_entry_name);

  /* Match subnodes against dir_entry. */
  SearchNode *matched_node = NULL;
  for(SearchNode *node = iterator->state.access.search.subnodes;
      node != NULL; node = node->next)
  {
    if(nodeMatches(node, dir_entry_name))
    {
      if(matched_node == NULL)
      {
        matched_node = node;
      }
      else
      {
        warnNodeMatches(node, dir_entry_name);
        warnNodeMatches(matched_node, dir_entry_name);
        die("ambiguous rules for path: \"%s\"", iterator->buffer.str);
      }
    }
  }

  if(matched_node != NULL)
  {
    return finishNodeStep(iterator, matched_node, matched_node->policy);
  }

  /* Skip current path, if no fallback policy was defined. */
  if(iterator->state.access.search.fallback_policy == BPOL_none)
  {
    return finishSearchStep(iterator);
  }

  /* Match against ignore expressions. */
  for(RegexList *element = iterator->ignore_expressions; element != NULL;
      element = element->next)
  {
    if(regexec(element->regex, iterator->buffer.str, 0, NULL, 0) == 0)
    {
      element->has_matched = true;
      return finishSearchStep(iterator);
    }
  }

  return finishNodeStep(iterator, NULL,
                        iterator->state.access.search.fallback_policy);
}

/** Completes a search step by directly accessing next node available in
  the search iterator.

  @param iterator A valid iterator which current state represents direct
  access. It will be destroyed, if this search step is the last step. In
  this case the returned SearchResult will have the type SRT_end_of_search.
*/
static SearchResult finishCurrentNode(SearchIterator *iterator)
{
  if(iterator->state.access.current_node == NULL)
  {
    return finishDirectory(iterator);
  }

  SearchNode *node = iterator->state.access.current_node;
  iterator->state.access.current_node = node->next;

  setPathToFile(iterator, node->name);

  if(sPathExists(str(iterator->buffer.str)))
  {
    return finishNodeStep(iterator, node, node->policy);
  }

  return finishCurrentNode(iterator);
}

/** Creates a new SearchIterator for searching the filesystem.

  @param root_node A search tree used for searching the filesystem. This
  tree will be modified during search to denote nodes that have matched an
  existing file. See the documentation of SearchNode for more informations.
  The returned SearchIterator will keep references into this tree, so make
  sure not to modify it as long as the iterator is in use.

  @return A new search iterator from which files and directories can be
  queried by using searchGetNext(). It will be automatically destroyed if
  the search has reached its end.
*/
SearchIterator *searchNew(SearchNode *root_node)
{
  SearchIterator *iterator = sMalloc(sizeof *iterator);

  /* Initialize string buffer. */
  iterator->buffer.capacity = 8;
  iterator->buffer.str = sMalloc(iterator->buffer.capacity);

  /* Initialize a search step into "/". */
  iterator->buffer.str[0] = '/';
  iterator->buffer.str[1] = '\0';
  iterator->buffer.length = 1;

  recursionStepRaw(iterator, root_node, root_node->policy);

  /* Prevent found paths from starting with two slashes. */
  iterator->state.path_length = 0;

  /* Store reference to ignore expression list. */
  iterator->ignore_expressions = *root_node->ignore_expressions;

  /* Initialise the state stack. */
  iterator->state_stack.used = 0;
  iterator->state_stack.capacity = 4;
  iterator->state_stack.state_array =
    sMalloc(sSizeMul(sizeof *iterator->state_stack.state_array,
                     iterator->state_stack.capacity));

  return iterator;
}

/** Queries the next file from the given search iterator.

  @param iterator A valid search iterator. If the search has reached its
  end, the iterator will be destroyed and the returned SearchResult will
  have the type SRT_end_of_search.

  @return The next search result.
*/
SearchResult searchGetNext(SearchIterator *iterator)
{
  if(iterator->state.is_dir_search)
  {
    return finishSearchStep(iterator);
  }

  return finishCurrentNode(iterator);
}
