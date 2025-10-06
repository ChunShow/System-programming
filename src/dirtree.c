//--------------------------------------------------------------------------------------------------
// System Programming                         I/O Lab                                     FALL 2025
//
/// @file
/// @brief resursively traverse directory tree and list all entries
/// @author <yourname>
/// @studid <studentid>
//--------------------------------------------------------------------------------------------------

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
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
const char *print_formats[8] = {
  "Name                                                        User:Group           Size    Blocks Type\n",
  "----------------------------------------------------------------------------------------------------\n",
  "%-54s  %8.8s:%-8.8s  %10llu  %8llu    %c\n",
  "%-51.51s...  %8.8s:%-8.8s  %10llu  %8llu    %c\n",
  "%-68s   %14llu %9llu\n\n",
  "%-65.65s...   %14llu %9llu\n\n",
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


/// @brief recursively process directory @a dn and print its tree
///
/// @param dn absolute or relative path string
/// @param pstr prefix string printed in front of each entry
/// @param stats pointer to statistics
/// @param flags output control flags (F_*)
void process_dir(const char *dn, const char *pstr, struct summary *stats, unsigned int flags)
{
  DIR *dir = NULL;                     ///< Pointer to the directory stream.
  struct dirent *entries = NULL;       ///< Dynamically allocated array of entries.
  size_t count = 0;                    ///< Number of entries read.

  dir = opendir(dn);
  if (!dir) {
    panic("No such directory.", dn);
  }

  /// Read all entries from the directory into a dynamically allocated array.
  entries = read_entries(dir, &count);
  if (!entries) {
    closedir(dir);
    panic("Memory allocation failure.", NULL);
  }

  /// If the directory is not empty, sort and print the entries.
  if (count > 0) {
    qsort(entries, count, sizeof(struct dirent), dirent_compare);

    int dd = dirfd(dir);               ///< Get the file descriptor for fstatat.

    for (size_t i = 0; i < count; i++) {
      struct stat sb;
      if (fstatat(dd, entries[i].d_name, &sb, 0) < 0) {
        perror(entries[i].d_name);     ///< Print error for the specific file and continue.
        continue;
      }

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
      char file_name[MAX_FILE_LEN];
      snprintf(file_name, sizeof(file_name), "%s%s", pstr, entries[i].d_name);

      /// Print the formatted output.
      const char *line_format = print_formats[2];
      if (strlen(file_name) > 54) line_format = print_formats[3];
      printf(line_format, file_name, user_name, group_name,
            (unsigned long long)sb.st_size, (unsigned long long)sb.st_blocks, type_char);

      if (type_char == 'd') {
        // Construct the path for the subdirectory
        char next_path[MAX_PATH_LEN];
        snprintf(next_path, sizeof(next_path), "%s/%s", dn, entries[i].d_name);
        
        // Construct the new prefix for the next level
        char next_pstr[MAX_PATH_LEN];
        snprintf(next_pstr, sizeof(next_pstr), "%s  ", pstr);
        
        // Recursive call
        process_dir(next_path, next_pstr, stats, flags);
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


  //
  // process each directory
  //
  for (int i = 0; i < ndir; i++) {
    struct summary dstat = {0};

    printf("%s", print_formats[0]);
    printf("%s", print_formats[1]);
    printf("%s\n", directories[0]);
  
    process_dir(directories[i], "  ", &dstat, flags);
    
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

