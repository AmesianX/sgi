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

#include <lib/hsm.h>

#include <string.h>
#include <getopt.h>

/*---------------------------------------------------------------------------

Test program used to test the DMAPI function dm_set_region().  The
command line is:

	get_region [-n nelem] [-s sid] pathname

where pathname is the name of a file, nelem is the number of regions to pass
in the call, and sid is the session ID whose events you you are interested in.

----------------------------------------------------------------------------*/

#ifndef linux
extern	char	*sys_errlist[];
#endif
extern  int     optind;
extern  char    *optarg;


char	*Progname;


static void
usage(void)
{
	fprintf(stderr, "usage:\t%s [-n nelem] [-s sid] pathname\n", Progname);
	exit(1);
}


int
main(
	int	argc, 
	char	**argv)
{
	dm_sessid_t	sid = DM_NO_SESSION;
	dm_region_t	*regbufp = NULL;
	char		*pathname = NULL;
	u_int		nelemp;
	u_int		nelem = 1;
	void		*hanp;
	size_t	 	hlen;
	char		*name;
	int		opt;
	int		i;

	if (Progname = strrchr(argv[0], '/')) {
		Progname++;
	} else {
		Progname = argv[0];
	}

	/* Crack and validate the command line options. */

	while ((opt = getopt(argc, argv, "n:s:")) != EOF) {
		switch (opt) {
		case 'n':
			nelem = atol(optarg);
			break;
		case 's':
			sid = atol(optarg);
			break;
		case '?':
			usage();
		}
	}
	if (optind + 1 != argc)
		usage();
	pathname = argv[optind++];

	if (dm_init_service(&name) == -1)  {
		fprintf(stderr, "Can't inititalize the DMAPI\n");
		exit(1);
	}
	if (sid == DM_NO_SESSION)
		find_test_session(&sid);

	/* Get the file's handle. */

	if (dm_path_to_handle(pathname, &hanp, &hlen)) {
		fprintf(stderr, "can't get handle for file %s\n", pathname);
		exit(1);
	}

	if (nelem > 0) {
		if ((regbufp = calloc(nelem, sizeof(*regbufp))) == NULL) {
			fprintf(stderr, "calloc failed, %s\n", strerror(errno));
			exit(1);
		}
	}

	if (dm_get_region(sid, hanp, hlen, DM_NO_TOKEN, nelem, regbufp,
	    &nelemp)) {
		fprintf(stderr, "dm_get_region failed, %s\n",
			strerror(errno));
		exit(1);
	}
	fprintf(stdout, "%d regions\n", nelemp);

	for (i = 0; i < nelemp; i++) {
#ifdef	VERITAS_21
		fprintf(stdout, "offset %d, size %d, flags 0x%x\n",
#else
		fprintf(stdout, "offset %lld, size %lld, flags 0x%x\n",
#endif
			regbufp[i].rg_offset, regbufp[i].rg_size,
			regbufp[i].rg_flags);
	}

	dm_handle_free(hanp, hlen);
	exit(0);
}
