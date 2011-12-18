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

/*---------------------------------------------------------------------------

Test program used to test the DMAPI function dm_get_dirattrs().  The
command line is:

	get_dirattrs [-b buflen] [-l loc] [-s sid] dirpath

where dirpath is the name of a directory, buflen is the size of the buffer
to use in the call, loc is a starting location, and sid is the session ID
whose attributes you are interested in.

----------------------------------------------------------------------------*/

extern	char	*sys_errlist[];
extern  int     optind;
extern  char    *optarg;


char	*Progname;

static void
usage(void)
{
	int	i;

	fprintf(stderr, "usage:\t%s [-b buflen] [-l loc] [-s sid] dirpath\n",
		Progname);
	exit(1);
}


int
main(
	int	argc, 
	char	**argv)
{
	dm_sessid_t	sid = DM_NO_SESSION;
	dm_attrloc_t	loc = 0;
	char		*dirpath;
	char		buffer[100];
	void		*bufp;
	size_t		buflen = 10000;
	u_int		mask;
	size_t		rlenp;
	void		*hanp;
	size_t		hlen;
	char		*name;
	int		error;
	int		opt;
	int		i;

	if (Progname = strrchr(argv[0], '/')) {
		Progname++;
	} else {
		Progname = argv[0];
	}

	/* Crack and validate the command line options. */

	while ((opt = getopt(argc, argv, "b:l:s:")) != EOF) {
		switch (opt) {
		case 'b':
			buflen = atol(optarg);
			break;
		case 'l':
			loc = atol(optarg);
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
	dirpath = argv[optind++];

	if (dm_init_service(&name) == -1)  {
		fprintf(stderr, "Can't inititalize the DMAPI\n");
		exit(1);
	}
	if (sid == DM_NO_SESSION)
		find_test_session(&sid);

	/* Get the diretory's handle. */

	if (dm_path_to_handle(dirpath, &hanp, &hlen)) {
		fprintf(stderr, "can't get handle for file %s, %s\n",
			dirpath, strerror(errno));
		exit(1);
	}

	if ((bufp = malloc(buflen == 0 ? 1 : buflen)) == NULL) {
		fprintf(stderr, "malloc failed, %s\n", strerror(errno));
		exit(1);
	}

	mask = DM_AT_HANDLE|DM_AT_EMASK|DM_AT_PMANR|DM_AT_PATTR|DM_AT_DTIME|DM_AT_CFLAG|DM_AT_STAT;

	if ((error = dm_get_dirattrs(sid, hanp, hlen, DM_NO_TOKEN, mask,
			&loc, buflen, bufp, &rlenp)) < 0) {
		if (errno == E2BIG) {
			fprintf(stderr, "dm_get_dirattrs buffer too small, "
				"should be %d bytes\n", rlenp);
		} else {
			fprintf(stderr, "dm_get_dirattrs failed, %s\n",
				strerror(errno));
		}
		exit(1);
	}
	fprintf(stdout, "rc = %d, rlenp is %d, loc is %lld\n", error,
		rlenp, loc);
	if (rlenp > 0) {
		dm_stat_t	*statp;

		statp = (dm_stat_t *)bufp;
		while (statp != NULL) {

			hantoa((char *)statp + statp->dt_handle.vd_offset,
				statp->dt_handle.vd_length, buffer);
			fprintf(stdout, "handle %s\n", buffer);
			fprintf(stdout, "name %s\n",
				(char *)statp + statp->dt_compname.vd_offset);
			print_line(statp);

			statp = DM_STEP_TO_NEXT(statp, dm_stat_t *);
		}
	}

	dm_handle_free(hanp, hlen);
	exit(0);
}
