/*
 * Copyright (c) 2000 Silicon Graphics, Inc.  All Rights Reserved.
 * 
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 * 
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * 
 * Further, this software is distributed without any warranty that it is
 * free of the rightful claim of any third person regarding infringement
 * or the like.  Any license provided herein, whether implied or
 * otherwise, applies only to this software file.  Patent licenses, if
 * any, provided herein do not apply to combinations of this program with
 * other software, or any other product whatsoever.
 * 
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston MA 02111-1307, USA.
 * 
 * Contact information: Silicon Graphics, Inc., 1600 Amphitheatre Pkwy,
 * Mountain View, CA  94043, or:
 * 
 * http://www.sgi.com 
 * 
 * For further information regarding this notice, see: 
 * 
 * http://oss.sgi.com/projects/GenInfo/SGIGPLNoticeExplan/
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <lib/dmport.h>

#include <getopt.h>
#ifdef linux
#include <xfs/handle.h>
#endif

/*---------------------------------------------------------------------------

Test program used to test the DMAPI function dm_handle_to_path().  The
command line is:

        handle_to_path [-b buflen] dirpath objpath

There are two parameters.  The first is the pathname of a directory,
and the second is the pathname of a file, directory, or symbolic link
within that directory.  The second parameter can also be the same as
the first if you want to specify "." (this is how EMASS uses it).
Pathnames can either be relative or full.

buflen is the size of the buffer to use in the call.

This program will return the full pathname of the object which is the
second parameter using the dm_handle_to_path() function.

The program should work successfully for files, directories, and
symbolic links, and does not have to work for any other type of
object.  It doesn't have to work across mount points.  There shouldn't
be any "/." crud on the end of the returned pathname either.

----------------------------------------------------------------------------*/

extern	int	optind;
extern	char	*optarg;


char	*Progname;


static void
usage(void)
{
	fprintf(stderr, "usage:\t%s [-b buflen] dirpath objpath\n", Progname);
	exit(1);
}


int
main(
	int		argc,
	char		**argv)
{
	char		*dirpath;
	char		*objpath;
	void		*hanp1, *hanp2, *hanp1a;
	size_t		hlen1, hlen2, hlen1a;
	void		*pathbufp;
	size_t		buflen = 1024;
	size_t		rlenp;
	char		*name;
	int		opt;

	if (Progname = strrchr(argv[0], '/')) {
		Progname++;
	} else {
		Progname = argv[0];
	}

	/* Crack and validate the command line options. */

	while ((opt = getopt(argc, argv, "b:")) != EOF) {
		switch (opt) {
		case 'b':
			buflen = atol(optarg);
			break;
		case '?':
			usage();
		}
	}
	if (optind + 2 != argc)
		usage();
	dirpath = argv[optind++];
	objpath = argv[optind];

	if (dm_init_service(&name)) {
		fprintf(stderr, "Can't inititalize the DMAPI\n");
		return(1);
	}

	if (dm_path_to_handle(dirpath, &hanp1, &hlen1)) {
		fprintf(stderr, "dm_path_to_handle failed for %s, (%d) %s\n",
			dirpath, errno, strerror(errno));
		return(1);
	}
	if (path_to_handle(dirpath, &hanp1a, &hlen1a)) {
		fprintf(stderr, "path_to_handle failed for %s, (%d) %s\n",
			dirpath, errno, strerror(errno));
		return(1);
	}
	if(hlen1 != hlen1a){
		fprintf(stderr, "dm_path_to_handle != path_to_handle, %d != %d\n",
			hlen1, hlen1a);
	}
	if( memcmp(hanp1, hanp1a, hlen1) != 0 ){
		fprintf(stderr, "dm_path_to_handle != path_to_handle, handles differ\n");
	}
	if (dm_path_to_handle(objpath, &hanp2, &hlen2)) {
		fprintf(stderr, "dm_path_to_handle failed for %s, (%d) %s\n",
			objpath, errno, strerror(errno));
		return(1);
	}

	if ((pathbufp = malloc(buflen == 0 ? 1 : buflen)) == NULL) {
		fprintf(stderr, "malloc failed\n");
		return(1);
	}

	if (dm_handle_to_path(hanp1, hlen1, hanp2, hlen2,
	    buflen, pathbufp, &rlenp)) {
		if (errno == E2BIG) {
			fprintf(stderr, "dm_handle_to_path buffer too small, "
				"should be %d bytes\n", rlenp);
		} else {
			fprintf(stderr, "dm_handle_to_path failed, (%d) %s\n",
				errno, strerror(errno));
		}
		return(1);
	}
	fprintf(stderr, "rlenp is %d, pathbufp is %s\n", rlenp, (char*)pathbufp);
	if (strlen(pathbufp) + 1 != rlenp) {
		fprintf(stderr, "rlenp is %d, should be %d\n", rlenp,
			strlen(pathbufp) + 1);
		return(1);
	}
	exit(0);
}
