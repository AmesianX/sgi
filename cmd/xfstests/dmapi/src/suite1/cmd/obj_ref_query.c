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

#include <getopt.h>
#include <string.h>


/*---------------------------------------------------------------------------

Test program used to test the DMAPI function dm_obj_ref_query().  The
command line is:

	obj_ref_query {-F} [-s sid] token {pathname|handle}

where:
-F
	when a pathname is specified, -F indicates that its filesystem handle
	should be used rather than its file object handle.
sid
	is the dm_sessid_t to use rather than the default test session.
token
	is the dm_token_t to use.
{pathname|handle}
	is either a handle, or is the pathname of a file whose handle is
	to be used.

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
	fprintf(stderr, "usage:\t%s [-F] [-s sid] token {pathname|handle}\n",
		Progname);
	exit(1);
}


int
main(
	int	argc,
	char	**argv)
{
	dm_sessid_t	sid = DM_NO_SESSION;
	dm_token_t	token;
	char		*object;
	void		*hanp;
	size_t	 	hlen;
	int		Fflag = 0;
	char		*name;
	int		error;
	int		opt;

	if (Progname = strrchr(argv[0], '/')) {
		Progname++;
	} else {
		Progname = argv[0];
	}

	/* Crack and validate the command line options. */

	while ((opt = getopt(argc, argv, "Fs:")) != EOF) {
		switch (opt) {
		case 'F':
			Fflag++;
			break;
		case 's':
			sid = atol(optarg);
			break;
		case '?':
			usage();
		}
	}
	if (optind + 2 != argc)
		usage();
	token = atol(argv[optind++]);
	object = argv[optind];

	if (dm_init_service(&name) == -1)  {
		fprintf(stderr, "Can't inititalize the DMAPI\n");
		exit(1);
	}
	if (sid == DM_NO_SESSION)
		find_test_session(&sid);

	/* Get the file or filesystem's handle. */

	if (opaque_to_handle(object, &hanp, &hlen)) {
		fprintf(stderr, "can't get handle from %s\n", object);
		exit(1);
	}
	if (Fflag) {
		void	*fshanp;
		size_t	fshlen;

		if (dm_handle_to_fshandle(hanp, hlen, &fshanp, &fshlen)) {
			fprintf(stderr, "can't get filesystem handle from %s\n",
				object);
			exit(1);
		}
		dm_handle_free(hanp, hlen);
		hanp = fshanp;
		hlen = fshlen;
	}

	if ((error = dm_obj_ref_query(sid, token, hanp, hlen)) < 0) {
		fprintf(stderr, "dm_obj_ref_query failed, %s\n",
			strerror(errno));
		return(1);
	}

	if (error == 1) {
		fprintf(stdout, "there is a hold\n");
	} else {
		fprintf(stdout, "there is no hold\n");
	}
	dm_handle_free(hanp, hlen);
	exit(0);
}
