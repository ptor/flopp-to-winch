/*
 * flopp-to-winch.c - Convert Norsk Data filesystem backups made with
 * the ND tool "WINCH-TO-FLOPP" back into an ND filesystem image. 
 * Copyright (c) 2020 Tor Arntsen <tor.arntsen@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. See the file "gpl.txt".
 * If not, see <http://www.gnu.org/licenses/>.
 */

#define VERSION "1.0"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#if defined(__sgi) || defined(__FreeBSD__) || defined(__DragonFly__)
# include <sys/endian.h>
#else
# if defined(__osf__)
#  include <machine/endian.h>
# else
#  if defined(__sun)
#   include <arpa/nameser_compat.h>
#  else
#   if defined(_AIX)
#    include <sys/machine.h>
#   else
#    include <endian.h>
#   endif
#  endif
# endif
#endif

/* Here 'cpu' means HOST cpu, not ND cpu. ND is BE (Big Endian). */
#if BYTE_ORDER==LITTLE_ENDIAN
# define be32_to_cpu(x) swap4((x))
# define be16_to_cpu(x) swap2((x))
#else
# define be32_to_cpu(x) (x)
# define be16_to_cpu(x) (x)
#endif

#define swap2(x) \
        ((unsigned short)((((unsigned short)(x) & 0x00ff) << 8) | \
                          (((unsigned short)(x) & 0xff00) >> 8)))

#define swap4(x) \
        ((unsigned int)((((unsigned int)(x) & 0x000000ffU) << 24) | \
                        (((unsigned int)(x) & 0x0000ff00U) <<  8) | \
                        (((unsigned int)(x) & 0x00ff0000U) >>  8) | \
                        (((unsigned int)(x) & 0xff000000U) >> 24)))

unsigned short
copy2 (const void *const buf, const int offset)
{
    unsigned short v;
    unsigned char * cv = (unsigned char *) &v;
    const unsigned char *const p = (unsigned char *) buf + offset;
    cv[0] = p[0];
    cv[1] = p[1];
    return (v);
} /* copy2 */

static unsigned int
copy4 (const void *const buf, const int offset)
{
    unsigned int v;
    const char *const p = (char *) buf + offset;
    memcpy (&v, p, 4);
    return (v);
} /* copy4 */

static void
copy_name (const unsigned char *const buf, char *const name, int size)
{
    int i;
    char c;
    for (i = 0; i < size; i++)
    {
	c = *(buf + i) & 0x7f;
	if (c == '\'')
	{
	    name[i] = '\0';
	    return;
	}
	else
	{
	    name[i] = c;
	}
    }
    name[size] = '\0';
    return;
} /* copy_name */

static int
update_image (int ifd, int vmaxpg, unsigned char hdr[16384],
	      const char *const output)
{
    struct stat st;
    int fd;
    int hdr_offset;
    int i;

    /*
     * Check if the output file exists already:
     */
    if (stat (output, &st) < 0)
    {
	/*
	 * No. Create it:
	 */
	if (creat (output, 0666) < 0)
	{
	    fprintf (stderr, "Cannot create %s: %s\n",
		     output, strerror (errno));
	    return (1);
	}
    }

    /*
     * Open the file for update:
     */
    fd = open (output, O_WRONLY);
    if (fd < 0)
    {
	fprintf (stderr, "Cannot open %s for writing: %s\n",
		 output, strerror (errno));
	return (1);
    }

    /*
     * Walk through the pages of the volume and put them where the
     * header indicates. Start reading the input file from after the
     * 16384 byte header.
     */

    if (lseek (ifd, 16384, SEEK_SET) != 16384)
    {
	fprintf (stderr, "Seek error in input file\n");
	close (fd);
	return (1);
    }

    hdr_offset = 76; /* Starting offset for page indices */
    i = 0;
    while (i < vmaxpg)
    {
	int pg_block[8];
	int pg_cnt;
	int pg_no;
	int j;
	for (j = 0; j < 8; j++)
	{
	    pg_block[j] = be32_to_cpu (copy4 (hdr, hdr_offset));
	    hdr_offset += 4;
	}
	pg_cnt = be32_to_cpu (copy4 (hdr, hdr_offset));
	hdr_offset += 4;
	if (pg_cnt == 0) /* Short volume */
	{
	    break;
	}
	for (j = 0; j < 8; j++)
	{
	    pg_no = pg_block[j];
	    if (pg_no == -1)
	    {
		/* Ignore */
	    }
	    else
	    {
		/*
		 * Place the current page at the position indicated:
		 */

		unsigned char page[2048];
		if (read (ifd, page, 2048) != 2048)
		{
		    fprintf (stderr, "Error reading input volume: %s\n",
			     strerror (errno));
		    close (fd);
		    return (1);
		}

		if (lseek (fd, (off_t) pg_no*2048, SEEK_SET) !=
		    (off_t) (pg_no*2048))
		{
		    fprintf (stderr, "Error seeking to page %d output file"
			     "%s\n", pg_no, strerror (errno));
		    close (fd);
		    return (1);
		}

		if (write (fd, page, 2048) != 2048)
		{
		    fprintf (stderr, "Error updating output file page %d:"
			     " %s\n", pg_no, strerror (errno));
		    close (fd);
		    return (1);
		}
	    }
	}
	i+= 8;
    } /* while */

    close (fd);
    return (0); /* Ok */
} /* static update_image */

static int
proc (const char *const volume, const char *const output)
{
    unsigned char buf[16384];
    int volcnt;
    int voltot;
    char dirname[16];
    char label[51];
    int pages;
    int offset;
    int cnt;
    int i;
    int blank;
    struct stat st;
    int fd;

    if (stat (volume, &st) < 0)
    {
	fprintf (stderr, "Can't open %s: %s\n", volume, strerror (errno));
	return (1);
    }

    if (st.st_size < 16384)
    {
	fprintf (stderr, "Illegal volume: %s\n", volume);
	return (1);
    }

    fd = open (volume, O_RDONLY);
    if (fd < 0)
    {
	fprintf (stderr, "Cannot open %s: %s\n", volume, strerror (errno));
	return (1);
    }

    if (read (fd, buf, 16384) != 16384)
    {
	fprintf (stderr, "Error reading %s: %s\n", volume, strerror (errno));
	close (fd);
	return (1);
    }

    /* Max number of pages in this particular volume: */
    if ((st.st_mode & S_IFMT) == S_IFREG) /* Regular file */
    {
	pages = (st.st_size - 16384) / 2048;
    }
    else
    {
	/*
	 * Probably reading directly from floppy. Assume it's a HD floppy
	 * which can hold 608 pages. No matter, the backup header should
	 * indicate when the floppy ends so that we won't read outside
	 * the image even if smaller.
	 */
	pages = 608;
    }

    volcnt = be16_to_cpu (copy2 (buf, 0));
    voltot = be16_to_cpu (copy2 (buf, 68));
    copy_name (buf+2, dirname, 16);
    memcpy (label, buf+18, 50);
    
    printf ("Vol %02d of %02d\n", volcnt, voltot);
    printf ("Dir %s\n", dirname);
    printf ("%s\n", label);

    if (output)
    {
	int res;
	res = update_image (fd, pages, buf, output);
	close (fd);
	return (res);
    }

    /*
     * If no output, just analyze.
     */
    close (fd); /* Not needed anymore here. */

    blank = 0;
    offset = 76; /* Starting offset for page indices */
    i = 0;
    while (i < pages)
    {
	int pg_block[8];
	int pg_cnt;
	int j;
	for (j = 0; j < 8; j++)
	{
	    pg_block[j] = be32_to_cpu (copy4 (buf, offset));
	    offset += 4;
	}
	pg_cnt = be32_to_cpu (copy4 (buf, offset));
	offset += 4;
	if (pg_cnt == 0) /* Short volume */
	{
	    break;
	}
	for (j = 0; j < 8; j++)
	{
	    cnt = pg_block[j];
	    if (cnt == -1) /* Ignore */
	    {
		blank++;
	    }
	}
	i+= 8;
    }
    printf ("%d pages\n", i - blank);
    return (0);
} /* proc */	

static void
usage (const char *const name)
{
    fprintf (stderr, "Usage: %s [options] <file1> [<file2> [<file3>]..]\n",
	     name);
    fprintf (stderr, "Options are:\n");
    fprintf (stderr, "-o <output file> Write decoded image to "
	     "<output file>\n");
    fprintf (stderr, "\tThe file is updated if it exists already.\n"
	     "\tIt is "
	     "possible to add volume files one or more at the time.\n");
    fprintf (stderr, "-h\tPrint this help and exit\n");
    fprintf (stderr, "-V\tShow version number and exit\n");
    fprintf (stderr, "\nThis tool recreates an ND filesystem image from "
	     "floppy disks or floppy disk\nimages originally made with "
	     "the SINTRAN-III backup utility \"WINCH-TO-FLOPP\"\n");
    fprintf (stderr, "If no output file is specified with -o then this "
	     "tool only writes information\nabout the backup volume(s).\n");
	     
    return;
} /* usage */

int
main (int argc, char *argv[])
{
    char *volumefile;
    char *output = NULL;
    int ind;
    int err;
    int c;
    int errflag = 0;

    while ((c = getopt (argc, argv, "o:Vh?")) != EOF)
    {
	switch (c)
	{
	case 'o': output = optarg;
	    break;
	case 'h':
	    errflag++;
	    break;
	case '?':
	    errflag++;
	    break;
	case 'V':
	    fprintf (stderr, "flopp-to-winch: Tool to restore image from "
		     "backup floppies, version %s\n", VERSION);
	    return (1);
	default:
	    errflag++;
	    break;
	}
    }

    if ((argc - optind) < 1)
    {
	errflag++;
    }

    if (errflag)
    {
	usage (argv[0]);
	return (1);
    }

    err = 0;
    ind = optind;
    while (ind < argc)
    {
	volumefile = argv[ind];
	err += proc (volumefile, output);
	if (err && (output != NULL))
	{
	    break; /* Don't continue if output and error */
	}
	ind++;
	if (ind < argc)
	{
	    printf ("\n");
	}
    }

    if (err)
    {
	return (1);
    }
    return (0);
}
