//--------------------------------------------------------------------------------------------------
// System Programming                         I/O Lab                                     FALL 2025
//
/// @file
/// @brief resursively traverse directory tree and list all entries
/// @author Junsu LEE
/// @studid 2018-11603
//--------------------------------------------------------------------------------------------------

#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <unistd.h>
#include <stdarg.h>
#include <assert.h>
#include <grp.h>
#include <pwd.h>
#include <fcntl.h>

/// @brief output control flags
#define F_DEPTH    0x1        ///< print directory tree
#define F_Filter   0x2        ///< pattern matching

/// @brief maximum numbers
#define MAX_DIR 64            ///< maximum number of supported directories
#define MAX_PATH_LEN 1024     ///< maximum length of a path
#define MAX_FILE_LEN 1024     ///< maximum length of a file
#define MAX_DEPTH 20          ///< maximum depth of directory tree (for -d option)
int max_depth = MAX_DEPTH;    ///< maximum depth of directory tree (for -d option)

/// @brief initial numbers
#define INITIAL_CAPACITY 64   ///< initial capacity for array

/// @brief struct holding the summary
struct summary {
  unsigned int dirs;          ///< number of directories encountered
  unsigned int files;         ///< number of files
  unsigned int links;         ///< number of links
  unsigned int fifos;         ///< number of pipes
  unsigned int socks;         ///< number of sockets

  unsigned long long size;    ///< total size (in bytes)
  unsigned long long blocks;  ///< total number of blocks (512 byte blocks)
};

/// @brief print strings used in the output
const char *print_formats[10] = {
  "Name                                                        User:Group           Size    Blocks Type\n",
  "----------------------------------------------------------------------------------------------------\n",
  "%-54s  %8.8s:%-8.8s  %10llu  %8llu    %c\n",
  "%-51.51s...  %8.8s:%-8.8s  %10llu  %8llu    %c\n",
  "%-68s   %14llu %9llu\n\n",
  "%-65.65s...   %14llu %9llu\n\n",
  "%-54s\n",
  "%-51.51s...\n",
  "Invalid pattern syntax",
};
const char* pattern = NULL;  ///< pattern for filtering entries

/// @brief abort the program with EXIT_FAILURE and an optional error message
///
/// @param msg optional error message or NULL
/// @param format optional format string (printf format) or NULL
void panic(const char* msg, const char* format)
{
  if (msg) {
    if (format) fprintf(stderr, format, msg);
    else        fprintf(stderr, "%s\n", msg);
  }
  exit(EXIT_FAILURE);
}


/// @brief read next directory entry from open directory 'dir'. Ignores '.' and '..' entries
///
/// @param dir open DIR* stream
/// @retval entry on success
/// @retval NULL on error or if there are no more entries
struct dirent *get_next(DIR *dir)
{
  struct dirent *next;
  int ignore;

  do {
    errno = 0;
    next = readdir(dir);
    if (errno != 0) perror(NULL);
    ignore = next && ((strcmp(next->d_name, ".") == 0) || (strcmp(next->d_name, "..") == 0));
  } while (next && ignore);

  return next;
}


/// @brief qsort comparator to sort directory entries. Sorted by name, directories first.
///
/// @param a pointer to first entry
/// @param b pointer to second entry
/// @retval -1 if a<b
/// @retval 0  if a==b
/// @retval 1  if a>b
static int dirent_compare(const void *a, const void *b)
{
  struct dirent *e1 = (struct dirent*)a;
  struct dirent *e2 = (struct dirent*)b;

  // if one of the entries is a directory, it comes first
  if (e1->d_type != e2->d_type) {
    if (e1->d_type == DT_DIR) return -1;
    if (e2->d_type == DT_DIR) return 1;
  }

  // otherwise sort by name
  return strcmp(e1->d_name, e2->d_name);
}


/// @brief Reads all directory entries into a dynamically allocated array.
///
/// @param dir An open directory stream.
/// @param count Pointer to store the final number of entries read.
/// @retval A pointer to the dynamically allocated array of dirent structs, or NULL on failure.
static struct dirent* read_entries(DIR *dir, size_t *count)
{
  size_t capacity = INITIAL_CAPACITY;
  struct dirent *entry = NULL;
  
  *count = 0;

  /// Allocate the initial array for the directory entries themselves.
  struct dirent *entries = malloc(capacity * sizeof(struct dirent));
  if (!entries) return NULL;

  while ((entry = get_next(dir)) != NULL) {
    /// If the array is full, double its capacity.
    if (*count == capacity) {
      capacity *= 2;
      struct dirent *tmp = realloc(entries, capacity * sizeof(struct dirent));
      if (!tmp) {
        free(entries);
        return NULL;
      }
      entries = tmp;
    }
    /// Copy the entry from the temporary buffer into our array.
    memcpy(&entries[*count], entry, sizeof(struct dirent));
    (*count)++;
  }

  return entries;
}


/// @brief The core recursive engine for anchored pattern matching.
/// @brief Checks if a pattern matches the beginning of a string.
///
/// @param str     The string to check.
/// @param pattern The pattern to match against.
/// @retval true   if the pattern is a prefix of the string.
/// @retval false  otherwise.
static bool submatch(const char *str, const char *pattern) {
  // Base case: If the pattern is exhausted, we have a successful match.
  if (*pattern == '\0') {
    return true;
  }

  // Case 1: The next pattern element is followed by a '*'
  if (*(pattern + 1) == '*') {

    // Option A: The element repeats zero times.
    if (submatch(str, pattern + 2)) {
      return true;
    }

    // Option B: The element repeats one or more times.
    if (*str != '\0' && (*pattern == '?' || *pattern == *str)) {
      return submatch(str + 1, pattern);
    }

    return false;
  }

  // Case 2: The pattern element is a group `()`
  if (*pattern == '(') {
    const char *end = strchr(pattern, ')');
    if (!end) return false;
    int group_len = end - (pattern + 1);

    char group_pattern[group_len + 1];
    memcpy(group_pattern, pattern + 1, group_len);
    group_pattern[group_len] = '\0';

    if (*end != '\0' && *(end + 1) == '*') {
      // Option A: The group repeats zero times.
      if (submatch(str, end + 2)) {
        return true;
      }

      // Option B: The group repeats one or more times.
      if (submatch(str, group_pattern)) {
        return submatch(str + group_len, pattern);
      }

    } else {
      // Match the group content exactly once.
      if (submatch(str, group_pattern)) {
        // Advance both string and pattern past the matched group.
        return submatch(str + group_len, end + 1);
      }
    }

    return false;
  }

  // Case 3: The pattern is a normal character or '?' (not followed by '*')
  if (*str != '\0' && (*pattern == '?' || *pattern == *str)) {
    return submatch(str + 1, pattern + 1);
  }

  return false; // No match found.
}


/// @brief Checks if a pattern exists as a substring within a string.
///
/// @param str The string to search within.
/// @param pattern The pattern to find.
/// @retval true if the pattern is found.
/// @retval false if the pattern is not found.
bool match(const char *str, const char *pattern)
{
  do {
    if (submatch(str, pattern)) {
      return true;
    }
  } while (*str++ != '\0');

  return false;
}


/// @brief recursively process directory @a dn and print its tree
///
/// @param dn absolute or relative path string
/// @param pstr prefix string printed in front of each entry
/// @param stats pointer to statistics
/// @param flags output control flags (F_*)
/// @param depth current recursion depth of the directory traversal
/// @param path_buffer buffer to store non-matching parent directory paths
/// @param p_buffer_count pointer to the count of items in path_buffer
void process_dir(const char *dn, const char *pstr, struct summary *stats, unsigned int flags, unsigned int depth,
                 char *path_buffer[], size_t *p_buffer_count)
{
  DIR *dir = NULL;                     ///< Pointer to the directory stream.
  struct dirent *entries = NULL;       ///< Dynamically allocated array of entries.
  size_t count = 0;                    ///< Number of entries read.

  dir = opendir(dn);
  if (!dir) {
    char error_msg[MAX_FILE_LEN];
    snprintf(error_msg, sizeof(error_msg), "%s%s", pstr, "ERROR");
    perror(error_msg);
    return;
  }

  /// Read all entries from the directory into a dynamically allocated array.
  entries = read_entries(dir, &count);
  if (!entries) {
    closedir(dir);
    panic("Memory allocation failure.", NULL);
  }

  if (count > 0) {
    qsort(entries, count, sizeof(struct dirent), dirent_compare);
  }

  int dd = dirfd(dir);

  for (size_t i = 0; i < count; i++) {
    struct stat sb;
    if (fstatat(dd, entries[i].d_name, &sb, AT_SYMLINK_NOFOLLOW) < 0) {
      perror(entries[i].d_name);     ///< Print error for the specific file and continue.
      continue;
    }

    bool is_dir = S_ISDIR(sb.st_mode);
    bool is_matched = false;
    if (flags & F_Filter) {
      is_matched = match(entries[i].d_name, pattern);
    } else {
      is_matched = true; // If filter is off, everything is a match.
    }
    char file_name[MAX_FILE_LEN];

    if (is_matched) {
      // 1. A match is found! First, print any buffered parent paths.
      if (*p_buffer_count > 0) {
        for (size_t j = 0; j < *p_buffer_count; j++) {
          const char *line_format = print_formats[6];
          if (strlen(path_buffer[j]) > 54) line_format = print_formats[7];
          printf(line_format, path_buffer[j]);
          free(path_buffer[j]);
          path_buffer[j] = NULL;
        }
        *p_buffer_count = 0;
      }

      // 2. Now, print the current matched entry.
      /// Add the stat info to the summary statistics.
      stats->size += sb.st_size;
      stats->blocks += sb.st_blocks;

      /// Determine the character representing the file type and update stats.
      char type_char = ' ';            ///< Default for regular files.
      if (S_ISREG(sb.st_mode)) {
        stats->files++;
      } else if (S_ISDIR(sb.st_mode)) {
        stats->dirs++;
        type_char = 'd';
      } else if (S_ISLNK(sb.st_mode)) {
        stats->links++;
        type_char = 'l';
      } else if (S_ISFIFO(sb.st_mode)) {
        stats->fifos++;
        type_char = 'p';
      } else if (S_ISSOCK(sb.st_mode)) {
        stats->socks++;
        type_char = 's';
      }

      /// Get user and group names from their IDs.
      struct passwd *pw = getpwuid(sb.st_uid);
      struct group  *gr = getgrgid(sb.st_gid);
      const char *user_name = (pw) ? pw->pw_name : "unknown";
      const char *group_name = (gr) ? gr->gr_name : "unknown";

      /// Create the full output name by prepending the prefix string.
      snprintf(file_name, sizeof(file_name), "%s%s", pstr, entries[i].d_name);

      /// Print the formatted output.
      const char *line_format = print_formats[2];
      if (strlen(file_name) > 54) line_format = print_formats[3];
      printf(line_format, file_name, user_name, group_name,
            (unsigned long long)sb.st_size, (unsigned long long)sb.st_blocks, type_char);

    } else if (is_dir) {
      // 3. Not a match, but it's a directory. Buffer its name and recurse.
      snprintf(file_name, sizeof(file_name), "%s%s", pstr, entries[i].d_name);

      if (*p_buffer_count < MAX_DEPTH) { // Prevent buffer overflow
        path_buffer[*p_buffer_count] = strdup(file_name); // strdup allocates and copies
        (*p_buffer_count)++;
      }
    }

    /// Check conditions for recursive call
    if (is_dir) {
      // Check if the current depth has reached the maximum allowed depth
      if (depth >= max_depth) {
        continue;
      }

      // Construct the path for the subdirectory
      char next_path[MAX_PATH_LEN];
      snprintf(next_path, sizeof(next_path), "%s/%s", dn, entries[i].d_name);
      
      // Construct the new prefix for the next level
      char next_pstr[MAX_PATH_LEN];
      snprintf(next_pstr, sizeof(next_pstr), "%s  ", pstr);
      
      // Recursive call
      size_t buffer_count_before = *p_buffer_count;
      process_dir(next_path, next_pstr, stats, flags, depth + 1, path_buffer, p_buffer_count);

      if (!is_matched && *p_buffer_count == buffer_count_before) {        
        (*p_buffer_count)--;
        free(path_buffer[*p_buffer_count]);
        path_buffer[*p_buffer_count] = NULL;
      }
    }
  }

  /// Centralized cleanup for all resources.
  free(entries);                       ///< Free the array of entries.
  closedir(dir);                       ///< Close the directory stream.
}


/// @brief print program syntax and an optional error message. Aborts the program with EXIT_FAILURE
///
/// @param argv0 command line argument 0 (executable)
/// @param error optional error (format) string (printf format) or NULL
/// @param ... parameter to the error format string
void syntax(const char *argv0, const char *error, ...)
{
  if (error) {
    va_list ap;

    va_start(ap, error);
    vfprintf(stderr, error, ap);
    va_end(ap);

    printf("\n\n");
  }

  assert(argv0 != NULL);

  fprintf(stderr, "Usage %s [-d depth] [-f pattern] [-h] [path...]\n"
                  "Recursively traverse directory tree and list all entries. If no path is given, the current directory\n"
                  "is analyzed.\n"
                  "\n"
                  "Options:\n"
                  " -d depth   | set maximum depth of directory traversal (1-%d)\n"
                  " -f pattern | filter entries using pattern (supports \'?\', \'*\', and \'()\')\n"
                  " -h         | print this help\n"
                  " path...    | list of space-separated paths (max %d). Default is the current directory.\n",
                  basename(argv0), MAX_DEPTH, MAX_DIR);

  exit(EXIT_FAILURE);
}


/// @brief return singular or plural form depending on count
///
/// @param count number to check
/// @param singular singular word
/// @param plural plural word
/// @retval singular if count == 1, otherwise plural
const char *pluralize(unsigned int count, const char *singular, const char *plural)
{
    return (count == 1) ? singular : plural;
}


/// @brief create a summary line string from a summary structure
///
/// @param s pointer to the summary structure
/// @retval pointer to a static string containing the summary line
char* make_summary_line(const struct summary *s)
{
    static char buf[256];
    snprintf(
      buf, sizeof(buf),
      "%u %s, %u %s, %u %s, %u %s, and %u %s",
      s->files, pluralize(s->files, "file", "files"),
      s->dirs,  pluralize(s->dirs,  "directory", "directories"),
      s->links, pluralize(s->links, "link", "links"),
      s->fifos, pluralize(s->fifos, "pipe", "pipes"),
      s->socks, pluralize(s->socks, "socket", "sockets")
    );
    return buf;
}


/// @brief update total summary by adding sub-summary's values
///
/// @param tstat pointer to the total (accumulating) summary structure
/// @param dstat pointer to the sub-summary structure whose values will be added
void update_summary(struct summary *tstat, const struct summary *dstat)
{
  if (!tstat || !dstat) return;
  
    tstat->dirs   += dstat->dirs;
    tstat->files  += dstat->files;
    tstat->links  += dstat->links;
    tstat->fifos  += dstat->fifos;
    tstat->socks  += dstat->socks;
    tstat->size   += dstat->size;
    tstat->blocks += dstat->blocks;
}


/// @brief evaluates a pattern for syntax errors and calls panic() if an error is found
void evaluate_pattern()
{
  if (!pattern) {
    return;
  }

  int len = strlen(pattern);

  if (len == 0) {
    panic(print_formats[8], NULL);
  }

  int paren_count = 0;

  for (int i = 0; i < len; i++) {
    char current = pattern[i];
    char prev = (i > 0) ? pattern[i - 1] : '\0';

    if (current == '(') {
      paren_count++;
      if (i + 1 < len && pattern[i + 1] == ')') {
        panic(print_formats[8], NULL);
      }
    } else if (current == ')') {
      if (paren_count == 0) {
        panic(print_formats[8], NULL);
      }
      paren_count--;
    } else if (current == '*') {
      if (i == 0) {
        panic(print_formats[8], NULL);
      }
      if (prev == '*' || prev == '(') {
        panic(print_formats[8], NULL);
      }
    }
  }

  if (paren_count != 0) {
    panic(print_formats[8], NULL);
  }
}


/// @brief program entry point
int main(int argc, char *argv[])
{
  //
  // default directory is the current directory (".")
  //
  const char CURDIR[] = ".";
  const char *directories[MAX_DIR];
  int   ndir = 0;

  struct summary tstat = { 0 }; // a structure to store the total statistics
  unsigned int flags = 0;

  //
  // parse arguments
  //
  for (int i = 1; i < argc; i++) {
    if (argv[i][0] == '-') {
      // format: "-<flag>"
      if (!strcmp(argv[i], "-d")) {
        flags |= F_DEPTH;
        if (++i < argc && argv[i][0] != '-') {
          max_depth = atoi(argv[i]);
          if (max_depth < 1 || max_depth > MAX_DEPTH) {
            syntax(argv[0], "Invalid depth value '%s'. Must be between 1 and %d.", argv[i], MAX_DEPTH);
          }
        } 
        else {
          syntax(argv[0], "Missing depth value argument.");
        }
      }
      else if (!strcmp(argv[i], "-f")) {
        if (++i < argc && argv[i][0] != '-') {
          flags |= F_Filter;
          pattern = argv[i];
        }
        else {
          syntax(argv[0], "Missing filtering pattern argument.");
        }
      }
      else if (!strcmp(argv[i], "-h")) syntax(argv[0], NULL);
      else syntax(argv[0], "Unrecognized option '%s'.", argv[i]);
    }
    else {
      // anything else is recognized as a directory
      if (ndir < MAX_DIR) {
        directories[ndir++] = argv[i];
      }
      else {
        fprintf(stderr, "Warning: maximum number of directories exceeded, ignoring '%s'.\n", argv[i]);
      }
    }
  }

  // if no directory was specified, use the current directory
  if (ndir == 0) directories[ndir++] = CURDIR;

  if (flags & F_Filter) {
    evaluate_pattern();
  }

  //
  // process each directory
  //
  for (int i = 0; i < ndir; i++) {
    struct summary dstat = {0};

    printf("%s", print_formats[0]);
    printf("%s", print_formats[1]);
    printf("%s\n", directories[i]);
  
    // Create the buffer and count variable on the stack.
    char *path_buffer[MAX_DEPTH] = { 0 };
    size_t path_buffer_count = 0;
    process_dir(directories[i], "  ", &dstat, flags, 1, path_buffer, &path_buffer_count);
    
    printf("%s", print_formats[1]);
    char *sline = make_summary_line(&dstat);
    const char *sformat = print_formats[4];
    if (strlen(sline) > 69) sformat = print_formats[5];
    printf(sformat, sline, dstat.size, dstat.blocks);

    update_summary(&tstat, &dstat);
  }


  //
  // print aggregate statistics if more than one directory was traversed
  //
  if (ndir > 1) {
    printf("Analyzed %d directories:\n"
      "  total # of files:        %16d\n"
      "  total # of directories:  %16d\n"
      "  total # of links:        %16d\n"
      "  total # of pipes:        %16d\n"
      "  total # of sockets:      %16d\n"
      "  total # of entries:      %16d\n"
      "  total file size:         %16llu\n"
      "  total # of blocks:       %16llu\n",
      ndir, tstat.files, tstat.dirs, tstat.links, tstat.fifos, tstat.socks,
      tstat.files + tstat.dirs + tstat.links + tstat.fifos + tstat.socks, 
      tstat.size, tstat.blocks);
  }

  //
  // that's all, folks!
  //
  return EXIT_SUCCESS;
}

