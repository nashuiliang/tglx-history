/* Generate kernel symbol version hashes.
   Copyright 1996, 1997 Linux International.

   New implementation contributed by Richard Henderson <rth@tamu.edu>
   Based on original work by Bjorn Ekwall <bj0rn@blox.se>

   This file is part of the Linux modutils.

   This program is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by the
   Free Software Foundation; either version 2 of the License, or (at your
   option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <stdarg.h>
#include <getopt.h>

#include "genksyms.h"

/*----------------------------------------------------------------------*/

#define HASH_BUCKETS  4099

static struct symbol *symtab[HASH_BUCKETS];
FILE *outfile, *debugfile;

int cur_line = 1;
char *cur_filename, *output_directory;

int flag_debug, flag_dump_defs, flag_warnings;
int checksum_version = 1, kernel_version = version(2,0,0);

static int errors;
static int nsyms;

static struct symbol *expansion_trail;
static const char *crc_prefix = "";

static const char * const symbol_type_name[] = {
  "normal", "typedef", "enum", "struct", "union"
};

/*----------------------------------------------------------------------*/

static const unsigned int crctab32[] =
{
  0x00000000U, 0x77073096U, 0xee0e612cU, 0x990951baU, 0x076dc419U,
  0x706af48fU, 0xe963a535U, 0x9e6495a3U, 0x0edb8832U, 0x79dcb8a4U,
  0xe0d5e91eU, 0x97d2d988U, 0x09b64c2bU, 0x7eb17cbdU, 0xe7b82d07U,
  0x90bf1d91U, 0x1db71064U, 0x6ab020f2U, 0xf3b97148U, 0x84be41deU,
  0x1adad47dU, 0x6ddde4ebU, 0xf4d4b551U, 0x83d385c7U, 0x136c9856U,
  0x646ba8c0U, 0xfd62f97aU, 0x8a65c9ecU, 0x14015c4fU, 0x63066cd9U,
  0xfa0f3d63U, 0x8d080df5U, 0x3b6e20c8U, 0x4c69105eU, 0xd56041e4U,
  0xa2677172U, 0x3c03e4d1U, 0x4b04d447U, 0xd20d85fdU, 0xa50ab56bU,
  0x35b5a8faU, 0x42b2986cU, 0xdbbbc9d6U, 0xacbcf940U, 0x32d86ce3U,
  0x45df5c75U, 0xdcd60dcfU, 0xabd13d59U, 0x26d930acU, 0x51de003aU,
  0xc8d75180U, 0xbfd06116U, 0x21b4f4b5U, 0x56b3c423U, 0xcfba9599U,
  0xb8bda50fU, 0x2802b89eU, 0x5f058808U, 0xc60cd9b2U, 0xb10be924U,
  0x2f6f7c87U, 0x58684c11U, 0xc1611dabU, 0xb6662d3dU, 0x76dc4190U,
  0x01db7106U, 0x98d220bcU, 0xefd5102aU, 0x71b18589U, 0x06b6b51fU,
  0x9fbfe4a5U, 0xe8b8d433U, 0x7807c9a2U, 0x0f00f934U, 0x9609a88eU,
  0xe10e9818U, 0x7f6a0dbbU, 0x086d3d2dU, 0x91646c97U, 0xe6635c01U,
  0x6b6b51f4U, 0x1c6c6162U, 0x856530d8U, 0xf262004eU, 0x6c0695edU,
  0x1b01a57bU, 0x8208f4c1U, 0xf50fc457U, 0x65b0d9c6U, 0x12b7e950U,
  0x8bbeb8eaU, 0xfcb9887cU, 0x62dd1ddfU, 0x15da2d49U, 0x8cd37cf3U,
  0xfbd44c65U, 0x4db26158U, 0x3ab551ceU, 0xa3bc0074U, 0xd4bb30e2U,
  0x4adfa541U, 0x3dd895d7U, 0xa4d1c46dU, 0xd3d6f4fbU, 0x4369e96aU,
  0x346ed9fcU, 0xad678846U, 0xda60b8d0U, 0x44042d73U, 0x33031de5U,
  0xaa0a4c5fU, 0xdd0d7cc9U, 0x5005713cU, 0x270241aaU, 0xbe0b1010U,
  0xc90c2086U, 0x5768b525U, 0x206f85b3U, 0xb966d409U, 0xce61e49fU,
  0x5edef90eU, 0x29d9c998U, 0xb0d09822U, 0xc7d7a8b4U, 0x59b33d17U,
  0x2eb40d81U, 0xb7bd5c3bU, 0xc0ba6cadU, 0xedb88320U, 0x9abfb3b6U,
  0x03b6e20cU, 0x74b1d29aU, 0xead54739U, 0x9dd277afU, 0x04db2615U,
  0x73dc1683U, 0xe3630b12U, 0x94643b84U, 0x0d6d6a3eU, 0x7a6a5aa8U,
  0xe40ecf0bU, 0x9309ff9dU, 0x0a00ae27U, 0x7d079eb1U, 0xf00f9344U,
  0x8708a3d2U, 0x1e01f268U, 0x6906c2feU, 0xf762575dU, 0x806567cbU,
  0x196c3671U, 0x6e6b06e7U, 0xfed41b76U, 0x89d32be0U, 0x10da7a5aU,
  0x67dd4accU, 0xf9b9df6fU, 0x8ebeeff9U, 0x17b7be43U, 0x60b08ed5U,
  0xd6d6a3e8U, 0xa1d1937eU, 0x38d8c2c4U, 0x4fdff252U, 0xd1bb67f1U,
  0xa6bc5767U, 0x3fb506ddU, 0x48b2364bU, 0xd80d2bdaU, 0xaf0a1b4cU,
  0x36034af6U, 0x41047a60U, 0xdf60efc3U, 0xa867df55U, 0x316e8eefU,
  0x4669be79U, 0xcb61b38cU, 0xbc66831aU, 0x256fd2a0U, 0x5268e236U,
  0xcc0c7795U, 0xbb0b4703U, 0x220216b9U, 0x5505262fU, 0xc5ba3bbeU,
  0xb2bd0b28U, 0x2bb45a92U, 0x5cb36a04U, 0xc2d7ffa7U, 0xb5d0cf31U,
  0x2cd99e8bU, 0x5bdeae1dU, 0x9b64c2b0U, 0xec63f226U, 0x756aa39cU,
  0x026d930aU, 0x9c0906a9U, 0xeb0e363fU, 0x72076785U, 0x05005713U,
  0x95bf4a82U, 0xe2b87a14U, 0x7bb12baeU, 0x0cb61b38U, 0x92d28e9bU,
  0xe5d5be0dU, 0x7cdcefb7U, 0x0bdbdf21U, 0x86d3d2d4U, 0xf1d4e242U,
  0x68ddb3f8U, 0x1fda836eU, 0x81be16cdU, 0xf6b9265bU, 0x6fb077e1U,
  0x18b74777U, 0x88085ae6U, 0xff0f6a70U, 0x66063bcaU, 0x11010b5cU,
  0x8f659effU, 0xf862ae69U, 0x616bffd3U, 0x166ccf45U, 0xa00ae278U,
  0xd70dd2eeU, 0x4e048354U, 0x3903b3c2U, 0xa7672661U, 0xd06016f7U,
  0x4969474dU, 0x3e6e77dbU, 0xaed16a4aU, 0xd9d65adcU, 0x40df0b66U,
  0x37d83bf0U, 0xa9bcae53U, 0xdebb9ec5U, 0x47b2cf7fU, 0x30b5ffe9U,
  0xbdbdf21cU, 0xcabac28aU, 0x53b39330U, 0x24b4a3a6U, 0xbad03605U,
  0xcdd70693U, 0x54de5729U, 0x23d967bfU, 0xb3667a2eU, 0xc4614ab8U,
  0x5d681b02U, 0x2a6f2b94U, 0xb40bbe37U, 0xc30c8ea1U, 0x5a05df1bU,
  0x2d02ef8dU
};

static inline unsigned long
partial_crc32_one(unsigned char c, unsigned long crc)
{
  return crctab32[(crc ^ c) & 0xff] ^ (crc >> 8);
}

static inline unsigned long
partial_crc32(const char *s, unsigned long crc)
{
  while (*s)
    crc = partial_crc32_one(*s++, crc);
  return crc;
}

static inline unsigned long
crc32(const char *s)
{
  return partial_crc32(s, 0xffffffff) ^ 0xffffffff;
}


/*----------------------------------------------------------------------*/

static inline enum symbol_type
map_to_ns(enum symbol_type t)
{
  if (t == SYM_TYPEDEF)
    t = SYM_NORMAL;
  else if (t == SYM_UNION)
    t = SYM_STRUCT;
  return t;
}

struct symbol *
find_symbol(const char *name, enum symbol_type ns)
{
  unsigned long h = crc32(name) % HASH_BUCKETS;
  struct symbol *sym;

  for (sym = symtab[h]; sym ; sym = sym->hash_next)
    if (map_to_ns(sym->type) == map_to_ns(ns) && strcmp(name, sym->name) == 0)
      break;

  return sym;
}

struct symbol *
add_symbol(const char *name, enum symbol_type type, struct string_list *defn, int is_extern)
{
  unsigned long h = crc32(name) % HASH_BUCKETS;
  struct symbol *sym;

  for (sym = symtab[h]; sym ; sym = sym->hash_next)
    if (map_to_ns(sym->type) == map_to_ns(type)
	&& strcmp(name, sym->name) == 0)
      {
	if (!equal_list(sym->defn, defn))
	  error_with_pos("redefinition of %s", name);
	return sym;
      }

  sym = xmalloc(sizeof(*sym));
  sym->name = name;
  sym->type = type;
  sym->defn = defn;
  sym->expansion_trail = NULL;
  sym->is_extern = is_extern;

  sym->hash_next = symtab[h];
  symtab[h] = sym;

  if (flag_debug)
    {
      fprintf(debugfile, "Defn for %s %s == <", symbol_type_name[type],  name);
      if (is_extern)
        fputs("extern ", debugfile);
      print_list(debugfile, defn);
      fputs(">\n", debugfile);
    }

  ++nsyms;
  return sym;
}


/*----------------------------------------------------------------------*/

inline void
free_node(struct string_list *node)
{
  free(node->string);
  free(node);
}

void
free_list(struct string_list *s, struct string_list *e)
{
  while (s != e)
    {
      struct string_list *next = s->next;
      free_node(s);
      s = next;
    }
}

inline struct string_list *
copy_node(struct string_list *node)
{
  struct string_list *newnode;

  newnode = xmalloc(sizeof(*newnode));
  newnode->string = xstrdup(node->string);
  newnode->tag = node->tag;

  return newnode;
}

struct string_list *
copy_list(struct string_list *s, struct string_list *e)
{
  struct string_list *h, *p;

  if (s == e)
    return NULL;

  p = h = copy_node(s);
  while ((s = s->next) != e)
    p = p->next = copy_node(s);
  p->next = NULL;

  return h;
}

int
equal_list(struct string_list *a, struct string_list *b)
{
  while (a && b)
    {
      if (a->tag != b->tag || strcmp(a->string, b->string))
	return 0;
      a = a->next;
      b = b->next;
    }

  return !a && !b;
}

static inline void
print_node(FILE *f, struct string_list *list)
{
  switch (list->tag)
    {
    case SYM_STRUCT:
      putc('s', f);
      goto printit;
    case SYM_UNION:
      putc('u', f);
      goto printit;
    case SYM_ENUM:
      putc('e', f);
      goto printit;
    case SYM_TYPEDEF:
      putc('t', f);
      goto printit;

    printit:
      putc('#', f);
    case SYM_NORMAL:
      fputs(list->string, f);
      break;
    }
}

void
print_list(FILE *f, struct string_list *list)
{
  struct string_list **e, **b;
  struct string_list *tmp, **tmp2;
  int elem = 1;

  if (list == NULL)
    {
      fputs("(nil)", f);
      return;
    }

  tmp = list;
  while((tmp = tmp->next) != NULL)
	  elem++;

  b = alloca(elem * sizeof(*e));
  e = b + elem;
  tmp2 = e - 1;

  (*tmp2--) = list;
  while((list = list->next) != NULL)
	  *(tmp2--) = list;

  while (b != e)
    {
      print_node(f, *b++);
      putc(' ', f);
    }
}

static unsigned long
expand_and_crc_list(struct string_list *list, unsigned long crc)
{
  struct string_list **e, **b;
  struct string_list *tmp, **tmp2;
  int elem = 1;

  if (!list)
    return crc;

  tmp = list;
  while((tmp = tmp->next) != NULL)
	  elem++;

  b = alloca(elem * sizeof(*e));
  e = b + elem;
  tmp2 = e - 1;

  *(tmp2--) = list;
  while ((list = list->next) != NULL)
    *(tmp2--) = list;

  while (b != e)
    {
      struct string_list *cur;
      struct symbol *subsym;

      cur = *(b++);
      switch (cur->tag)
	{
	case SYM_NORMAL:
	  if (flag_dump_defs)
	    fprintf(debugfile, "%s ", cur->string);
	  crc = partial_crc32(cur->string, crc);
	  crc = partial_crc32_one(' ', crc);
	  break;

	case SYM_TYPEDEF:
	  subsym = find_symbol(cur->string, cur->tag);
	  if (checksum_version == 1)
	    crc = expand_and_crc_list(subsym->defn, crc);
	  else if (subsym->expansion_trail)
	    {
	      if (flag_dump_defs)
		fprintf(debugfile, "%s ", cur->string);
	      crc = partial_crc32(cur->string, crc);
	      crc = partial_crc32_one(' ', crc);
	    }
	  else
	    {
	      subsym->expansion_trail = expansion_trail;
	      expansion_trail = subsym;
	      crc = expand_and_crc_list(subsym->defn, crc);
	    }
	  break;

	case SYM_STRUCT:
	case SYM_UNION:
	case SYM_ENUM:
	  subsym = find_symbol(cur->string, cur->tag);
	  if (!subsym)
	    {
	      struct string_list *n, *t = NULL;

	      error_with_pos("expand undefined %s %s",
			     symbol_type_name[cur->tag], cur->string);

	      n = xmalloc(sizeof(*n));
	      n->string = xstrdup(symbol_type_name[cur->tag]);
	      n->tag = SYM_NORMAL;
	      n->next = t;
	      t = n;

	      n = xmalloc(sizeof(*n));
	      n->string = xstrdup(cur->string);
	      n->tag = SYM_NORMAL;
	      n->next = t;
	      t = n;

	      n = xmalloc(sizeof(*n));
	      n->string = xstrdup("{ UNKNOWN }");
	      n->tag = SYM_NORMAL;
	      n->next = t;

	      subsym = add_symbol(cur->string, cur->tag, n, 0);
	    }
	  if (subsym->expansion_trail)
	    {
	      if (flag_dump_defs)
		{
		  fprintf(debugfile, "%s %s ", symbol_type_name[cur->tag],
			  cur->string);
		}

	      crc = partial_crc32(symbol_type_name[cur->tag], crc);
	      crc = partial_crc32_one(' ', crc);
	      crc = partial_crc32(cur->string, crc);
	      crc = partial_crc32_one(' ', crc);
	    }
	  else
	    {
	      subsym->expansion_trail = expansion_trail;
	      expansion_trail = subsym;
	      crc = expand_and_crc_list(subsym->defn, crc);
	    }
	  break;
	}
    }

  return crc;
}

void
export_symbol(const char *name)
{
  struct symbol *sym;

  sym = find_symbol(name, SYM_NORMAL);
  if (!sym)
    error_with_pos("export undefined symbol %s", name);
  else
    {
      unsigned long crc;

      if (flag_dump_defs)
	fprintf(debugfile, "Export %s == <", name);

      expansion_trail = (struct symbol *)-1L;

      crc = expand_and_crc_list(sym->defn, 0xffffffff) ^ 0xffffffff;

      sym = expansion_trail;
      while (sym != (struct symbol *)-1L)
	{
	  struct symbol *n = sym->expansion_trail;
	  sym->expansion_trail = 0;
	  sym = n;
	}

      if (flag_dump_defs)
	fputs(">\n", debugfile);

      if (checksum_version > 1)
	{
	  fprintf(outfile, "#define __ver_%s\t%s%08lx\n", name,
		  crc_prefix, crc);
	  fprintf(outfile, "#define %s\t_set_ver(%s)\n", name, name);
	}
      else
	{
	  fprintf(outfile, "#define %s\t_set_ver(%s, %s%08lx)\n", name, name,
		  crc_prefix, crc);
	}
    }
}


/*----------------------------------------------------------------------*/

static int
parse_kernel_version(char * p)
{
  int a, b, c;

  a = strtoul(p, &p, 10);
  if (*p != '.')
    return -1;
  b = strtoul(p+1, &p, 10);
  if (*p != '.')
    return -1;
  c = strtoul(p+1, &p, 10);
  if (*p != '\0')
    return -1;

  kernel_version = a << 16 | b << 8 | c;

  return 0;
}

void
error(const char *fmt, ...)
{
  va_list args;

  if (flag_warnings)
    {
      va_start(args, fmt);
      vfprintf(stderr, fmt, args);
      va_end(args);
      putc('\n', stderr);

      errors++;
    }
}

void
error_with_pos(const char *fmt, ...)
{
  va_list args;

  if (flag_warnings)
    {
      fprintf(stderr, "%s:%d: ", cur_filename ? : "<stdin>", cur_line);

      va_start(args, fmt);
      vfprintf(stderr, fmt, args);
      va_end(args);
      putc('\n', stderr);

      errors++;
    }
}


void genksyms_usage(void)
{
	fputs("Usage:\n"
	      "genksyms [-dDwqhV] [-k kernel_version] [-p prefix] > .../linux/module/*.ver\n"
	      "\n"
	      "  -d, --debug           Increment the debug level (repeatable)\n"
	      "  -D, --dump            Dump expanded symbol defs (for debugging only)\n"
	      "  -w, --warnings        Enable warnings\n"
	      "  -q, --quiet           Disable warnings (default)\n"
	      "  -h, --help            Print this message\n"
	      "  -V, --version         Print the release version\n"
	      "  -k ver\n"
	      "       --kernel ver     Set the kernel version for which we are compiling\n"
	      "  -p string\n"
	      "       --prefix string  Set a mangling prefix for all symbols\n"
	      , stderr);
}

int
main(int argc, char **argv)
{
  int o;

  struct option long_opts[] = {
    {"debug", 0, 0, 'd'},
    {"warnings", 0, 0, 'w'},
    {"quiet", 0, 0, 'q'},
    {"dump", 0, 0, 'D'},
    {"kernel", 1, 0, 'k'},
    {"prefix", 1, 0, 'p'},
    {"version", 0, 0, 'V'},
    {"help", 0, 0, 'h'},
    {0, 0, 0, 0}
  };

  while ((o = getopt_long(argc, argv, "dwqVDk:p:",
			  &long_opts[0], NULL)) != EOF)
    switch (o)
      {
      case 'd':
	flag_debug++;
	break;
      case 'w':
	flag_warnings = 1;
	break;
      case 'q':
	flag_warnings = 0;
	break;
      case 'V':
	fputs("genksyms version " MODUTILS_VERSION "\n", stderr);
	break;
      case 'D':
	flag_dump_defs = 1;
	break;
      case 'k':
	if (parse_kernel_version(optarg)) {
	    fprintf( stderr, "unrecognised kernel version : %s\n", optarg);
	    return -1;
	}
	break;
      case 'p':
	crc_prefix = optarg;
	break;
      case 'h':
	genksyms_usage();
	return 0;
      default:
	genksyms_usage();
	return 1;
      }

  if (kernel_version >= version(2,1,18))
    {
      if (optind != argc) {
	  genksyms_usage();
	  return 1;
      }

      /* For newer kernels, eliminate some irrelevant constructs.  */
      checksum_version = 2;

      outfile = stdout;
    }
  else
    {
      if (optind+1 != argc) {
	  genksyms_usage();
	  return 1;
      }
      output_directory = argv[optind];
    }

    {
      extern int yydebug;
      extern int yy_flex_debug;

      yydebug = (flag_debug > 1);
      yy_flex_debug = (flag_debug > 2);

      debugfile = stderr;
      /* setlinebuf(debugfile); */
    }

  yyparse();

  if (checksum_version == 1)
    {
      fputs("#endif\n#endif\n", outfile);
    }

  if (flag_debug)
    {
      fprintf(debugfile, "Hash table occupancy %d/%d = %g\n",
	      nsyms, HASH_BUCKETS, (double)nsyms / (double)HASH_BUCKETS);
    }

  return errors != 0;
}
